"""Run via portable qrenderdoc --py; request/options in artifact replay-request.json."""
import json
import pathlib
import sys
import traceback
import renderdoc as rd

root = pathlib.Path(__file__).resolve().parents[1] / 'docs/bringup-artifacts/nvidia-minimap'
log = (root / 'replay.log').open('w', buffering=1)
sys.stdout = sys.stderr = log
cap = controller = None
try:
    req = json.loads((root / 'replay-request.json').read_text())
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(req['capture'], '', None)
    print('open', status)
    assert status == rd.ResultCode.Succeeded
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    print('replay', status)
    assert status == rd.ResultCode.Succeeded
    resources = {str(r.resourceId): r.name for r in controller.GetResources()}
    textures = {str(t.resourceId): {'width': t.width, 'height': t.height,
                'samples': t.msSamp, 'format': t.format.Name()} for t in controller.GetTextures()}
    (root / 'capture-textures.json').write_text(json.dumps(textures, indent=2))
    actions = []
    def visit(nodes):
        for a in nodes:
            if a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch | rd.ActionFlags.Clear | rd.ActionFlags.Copy | rd.ActionFlags.Resolve | rd.ActionFlags.Present):
                actions.append({'event': a.eventId, 'name': a.GetName(controller.GetStructuredFile()),
                    'flags': str(a.flags), 'indices': a.numIndices,
                    'outputs': [str(r) for r in a.outputs], 'depth': str(a.depthOut)})
            visit(a.children)
    visit(controller.GetRootActions())
    (root / 'capture-actions.json').write_text(json.dumps(actions, indent=2))
    print('actions', len(actions))
    for h in req.get('histories', []):
        texture = next(t.resourceId for t in controller.GetTextures() if str(t.resourceId) == h['texture'])
        controller.SetFrameEvent(h.get('event', actions[-1]['event']), True)
        history = controller.PixelHistory(texture, h['x'], h['y'], rd.Subresource(), rd.CompType.Typeless)
        (root / ('history-%s-%d-%d.txt' % (h['texture'].split('::')[-1], h['x'], h['y']))).write_text(json.dumps([rd.DumpObject(m) for m in history], indent=2, default=str))
    for event in req.get('events', []):
        controller.SetFrameEvent(event, True)
        pipe = controller.GetPipelineState()
        state = controller.GetVulkanPipelineState()
        (root / ('pipeline-%d.txt' % event)).write_text(json.dumps(rd.DumpObject(state), indent=2, default=str))
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
            refl = pipe.GetShaderReflection(stage)
            if refl:
                dis = controller.DisassembleShader(pipe.GetGraphicsPipelineObject(), refl, '')
                (root / ('shader-%d-%s.txt' % (event, stage))).write_text(dis)
        for i, target in enumerate(pipe.GetOutputTargets()):
            if target.resource == rd.ResourceId.Null(): continue
            save = rd.TextureSave()
            save.resourceId = target.resource
            save.destType = rd.FileType.PNG
            save.sample.sampleIndex = 0xffffffff
            result = controller.SaveTexture(save, str(root / ('event-%d-target-%d.png' % (event, i))))
            print('saved', event, i, target.resource, result)
    print('messages', [(m.eventID, m.description) for m in controller.GetDebugMessages()])
except Exception:
    traceback.print_exc()
finally:
    if controller: controller.Shutdown()
    if cap: cap.Shutdown()
    log.flush()
sys.exit(0)
