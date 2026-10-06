"""Read-only GDB extraction of the retained deferred stream; never calls inferior code."""
import gdb
import json
import re
from collections import Counter
from pathlib import Path

gdb.execute('set may-call-functions off')

ROOT = Path('docs/bringup-artifacts/present-semaphore-fix/nvidia-optimized-1/paused-inspection')
ROOT.mkdir(exist_ok=True)
PREFIX = 'rex::graphics::vulkan::DeferredCommandBuffer::'
header = (Path.home() / 'rexglue-sdk/include/rex/graphics/vulkan/deferred_command_buffer.h').read_text()
names = re.findall(r'\bkVk\w+', header.split('enum class Command {')[1].split('};')[0])
inferior = gdb.selected_inferior()

def obj(address, typename):
    return gdb.Value(address).cast(gdb.lookup_type(typename).pointer()).dereference()

def convert(v, depth=0):
    t = v.type.strip_typedefs()
    if t.code == gdb.TYPE_CODE_PTR: return hex(int(v))
    if t.code == gdb.TYPE_CODE_ENUM: return str(v)
    if t.code == gdb.TYPE_CODE_ARRAY:
        lo, hi = t.range()
        return [convert(v[i], depth+1) for i in range(lo,hi+1)]
    if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        if depth > 8: return str(v)
        return {f.name:convert(v[f.name],depth+1) for f in t.fields() if f.name and not f.is_base_class}
    if t.code == gdb.TYPE_CODE_FLT: return float(v)
    try: return int(v)
    except Exception: return str(v)

stream = gdb.parse_and_eval('$xenos_cp->deferred_command_buffer_.command_stream_._M_impl')
start, finish = int(stream['_M_start']), int(stream['_M_finish'])
ROOT.joinpath('deferred-stream.bin').write_bytes(bytes(inferior.read_memory(start,finish-start)))
commands=[]
p=start
while p < finish:
    h = obj(p,PREFIX+'CommandHeader')
    name=names[int(h['command'])][1:]
    size=int(h['arguments_size_elements'])*8
    args_addr=p+8
    assert args_addr+size <= finish
    entry={'index':len(commands),'offset':p-start,'command':name,'argument_bytes':size}
    if size:
        typename='ArgsSetStencilMaskReference' if name.startswith('VkSetStencil') else 'Args'+name
        args=obj(args_addr,PREFIX+typename)
        entry['args']=convert(args)
        cursor=args_addr+args.type.sizeof
        def read_array(typename,count,cursor):
            ty=gdb.lookup_type(typename); alignment=ty.alignof
            cursor=(cursor+alignment-1)//alignment*alignment
            end=cursor+ty.sizeof*count
            assert end<=args_addr+size,(name,typename,end,args_addr+size)
            return [convert(obj(cursor+i*ty.sizeof,typename)) for i in range(count)],end
        arrays=[]
        if name=='VkPipelineBarrier':
            arrays=[('memory_barriers','VkMemoryBarrier',int(args['memory_barrier_count'])),('buffer_barriers','VkBufferMemoryBarrier',int(args['buffer_memory_barrier_count'])),('image_barriers','VkImageMemoryBarrier',int(args['image_memory_barrier_count']))]
        elif name=='VkBeginRendering':
            arrays=[('color_attachments','VkRenderingAttachmentInfo',int(args['color_attachment_count'])),('depth_attachment','VkRenderingAttachmentInfo',int(args['has_depth_attachment'])),('stencil_attachment','VkRenderingAttachmentInfo',int(args['has_stencil_attachment']))]
        elif name=='VkBindDescriptorSets':
            arrays=[('descriptor_sets','VkDescriptorSet',int(args['descriptor_set_count'])),('dynamic_offsets','uint32_t',int(args['dynamic_offset_count']))]
        elif name=='VkClearAttachments': arrays=[('attachments','VkClearAttachment',int(args['attachment_count'])),('rects','VkClearRect',int(args['rect_count']))]
        elif name=='VkCopyBuffer': arrays=[('regions','VkBufferCopy',int(args['region_count']))]
        elif name=='VkCopyBufferToImage': arrays=[('regions','VkBufferImageCopy',int(args['region_count']))]
        elif name=='VkSetViewport': arrays=[('viewports','VkViewport',int(args['viewport_count']))]
        elif name=='VkSetScissor': arrays=[('scissors','VkRect2D',int(args['scissor_count']))]
        elif name=='VkBindVertexBuffers': arrays=[('buffers','VkBuffer',int(args['binding_count'])),('offsets','VkDeviceSize',int(args['binding_count']))]
        for key,ty,count in arrays:
            if count: entry[key],cursor=read_array(ty,count,cursor)
        if name=='VkPushConstants': entry['data_hex']=bytes(inferior.read_memory(cursor,int(args['size']))).hex()
    commands.append(entry)
    p=args_addr+size
assert p==finish
ROOT.joinpath('deferred-stream.json').write_text(json.dumps(commands,indent=2))
gdb.write('Decoded '+str(len(commands))+' commands, '+str(finish-start)+' bytes: '+str(dict(Counter(c['command'] for c in commands)))+'\n')

# STL values are traversed by debugger pretty-printers, never inferior methods.
def read_container(expression):
    value=gdb.parse_and_eval(expression)
    printer=gdb.default_visualizer(value)
    return [(name,convert(v)) for name,v in printer.children()]

snapshot={}
for member in ('command_buffers_submitted_','command_buffers_writable_','fences_free_',
               'submissions_in_flight_fences_','submissions_in_flight_semaphores_',
               'semaphores_free_','destroy_images_','destroy_image_views_',
               'destroy_buffers_','destroy_memory_','destroy_framebuffers_',
               'single_transient_descriptors_used_','constants_transient_descriptors_used_',
               'texture_transient_descriptor_sets_used_','constants_transient_descriptors_free_'):
    try: snapshot[member]=read_container('$xenos_cp->'+member)
    except Exception as e: snapshot[member]={'unavailable':str(e)}
for member in ('pipelines_','deferred_destroy_pipelines_'):
    try: snapshot['pipeline_'+member]=read_container('$xenos_cp->pipeline_cache_->'+member)
    except Exception as e: snapshot['pipeline_'+member]={'unavailable':str(e)}
for member in ('dump_pipelines_','direct_resolve_pipelines_','transfer_pipelines_'):
    try: snapshot['rt_'+member]=read_container('$xenos_cp->render_target_cache_->'+member)
    except Exception as e: snapshot['rt_'+member]={'unavailable':str(e)}

ROOT.joinpath('container-snapshot.json').write_text(json.dumps(snapshot,indent=2))
rt_values=list(gdb.default_visualizer(gdb.parse_and_eval('$xenos_cp->render_target_cache_->render_targets_')).children())
snapshot['render_targets']=[]
for i in range(0,len(rt_values),2):
    key,ptr=rt_values[i][1],rt_values[i+1][1]
    derived=ptr.cast(gdb.lookup_type('rex::graphics::vulkan::VulkanRenderTargetCache::VulkanRenderTarget').pointer()).dereference()
    snapshot['render_targets'].append({'key':str(key),'address':hex(int(ptr)),'state':convert(derived)})
    ROOT.joinpath('render-target-'+hex(int(ptr))+'.txt').write_text(gdb.execute('p *('+str(derived.type)+'*)'+hex(int(ptr)),to_string=True))

snapshot['upload_pools']={}
for name,expr in [('shared','$xenos_cp->shared_memory_->upload_buffer_pool_'),('uniform','$xenos_cp->uniform_buffer_pool_'),('transfer','$xenos_cp->render_target_cache_->transfer_vertex_buffer_pool_')]:
    pool=gdb.parse_and_eval('*'+expr)
    pages=[]
    for kind in ('writable_first_','submitted_first_'):
        ptr=pool[kind]; seen=set()
        while int(ptr):
            assert int(ptr) not in seen
            seen.add(int(ptr))
            page=ptr.cast(gdb.lookup_type('rex::ui::vulkan::VulkanUploadBufferPool::VulkanPage').pointer()).dereference()
            pages.append(dict(list=kind,address=hex(int(ptr)),buffer=hex(int(page['buffer_'])),memory=hex(int(page['memory_'])),last_index=int(page['last_submission_index_']),mapping=hex(int(page['mapping_']))))
            ptr=page['next_']
    snapshot['upload_pools'][name]={'page_size':int(pool['page_size_']),'current_used':int(pool['current_page_used_']),'current_flushed':int(pool['current_page_flushed_']),'pages':pages}
ROOT.joinpath('ownership.json').write_text(json.dumps(snapshot,indent=2))
gdb.write('Saved ownership and render-target snapshots.\n')
