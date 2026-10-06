"""Read-only GDB capture: stop at the first Vulkan validation error."""
import gdb
import json
from collections import deque
from pathlib import Path

history = deque(maxlen=96)
artifact_directory = gdb.convenience_variable('vk_artifact_dir')
artifact_directory = Path(artifact_directory.string() if artifact_directory is not None else
                          'docs/bringup-artifacts/vulkan-device-loss')
trace_api = gdb.convenience_variable('vk_trace_api')
trace_api = True if trace_api is None else bool(int(trace_api))


def value(expression):
    return gdb.parse_and_eval(expression)


def handles(pointer, count):
    return [hex(int(pointer[i])) for i in range(int(count))]


class Returned(gdb.FinishBreakpoint):
    def __init__(self, event):
        super().__init__(gdb.newest_frame(), internal=True)
        self.event = event

    def stop(self):
        result = int(value('(int)$eax'))
        self.event['result'] = result
        if result < 0:
            gdb.write('NEGATIVE VULKAN RESULT '+json.dumps(self.event)+'\n')
            return True
        return False


class ApiCall(gdb.Breakpoint):
    def __init__(self, name, address):
        super().__init__('*'+hex(int(address)), internal=True)
        self.api = name

    def stop(self):
        event = dict(api=self.api, thread=gdb.selected_thread().global_num)
        if self.api == 'vkQueueSubmit':
            event.update(queue=hex(int(value('$rdi'))), fence=hex(int(value('$rcx'))), submits=[])
            packets = value('(VkSubmitInfo*)$rdx')
            for i in range(int(value('$esi'))):
                packet = packets[i]
                event['submits'].append(dict(
                    wait=handles(packet['pWaitSemaphores'], packet['waitSemaphoreCount']),
                    stages=handles(packet['pWaitDstStageMask'], packet['waitSemaphoreCount']),
                    signal=handles(packet['pSignalSemaphores'], packet['signalSemaphoreCount']),
                    commands=handles(packet['pCommandBuffers'], packet['commandBufferCount'])))
        elif self.api == 'vkQueuePresentKHR':
            packet = value('(VkPresentInfoKHR*)$rsi').dereference()
            event.update(queue=hex(int(value('$rdi'))),
                wait=handles(packet['pWaitSemaphores'], packet['waitSemaphoreCount']),
                swapchains=handles(packet['pSwapchains'], packet['swapchainCount']),
                images=[int(packet['pImageIndices'][i]) for i in range(int(packet['swapchainCount']))])
        elif self.api == 'vkWaitForFences':
            event.update(fences=handles(value('(VkFence*)$rdx'), value('$esi')),
                         wait_all=int(value('$ecx')), timeout=int(value('$r8')))
        else:
            event['object'] = hex(int(value('$rdi' if self.api == 'vkEndCommandBuffer' else '$rsi')))
        history.append(event)
        Returned(event)
        return False


class RememberObject(gdb.Breakpoint):
    def __init__(self, symbol, variable, type_name):
        super().__init__(symbol, internal=True)
        self.variable, self.type_name = variable, type_name

    def stop(self):
        ptr = value('('+self.type_name+'*)$rdi')
        gdb.set_convenience_variable(self.variable, ptr)
        self.enabled = False
        if self.variable == 'presenter' and trace_api:
            for name in ('vkQueueSubmit', 'vkQueuePresentKHR', 'vkWaitForFences',
                         'vkGetFenceStatus', 'vkResetCommandPool', 'vkEndCommandBuffer'):
                ApiCall(name, ptr.dereference()['vulkan_device_'].dereference()['functions_'][name])
        return False


class ValidationError(gdb.Breakpoint):
    def stop(self):
        try:
            severity = int(gdb.parse_and_eval('message_severity'))
        except gdb.error:
            severity = int(gdb.parse_and_eval('$edi'))
        if not severity & 0x1000:
            return False
        try:
            data = gdb.parse_and_eval('callback_data')
        except gdb.error:
            data = gdb.parse_and_eval('(VkDebugUtilsMessengerCallbackDataEXT*)$rdx')
        gdb.set_convenience_variable('validation_data', data)
        gdb.write('\nFIRST VULKAN VALIDATION ERROR\n')
        gdb.write(data.dereference()['pMessage'].string()+'\n')
        return True


def show(command):
    try:
        gdb.write('\n> '+command+'\n')
        gdb.execute(command)
    except gdb.error as error:
        gdb.write('Unavailable: '+str(error)+'\n')


class Capture(gdb.Command):
    def __init__(self):
        super().__init__('capture-vulkan-state', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        original = gdb.selected_thread()
        (artifact_directory / 'api-history.json').write_text(
            json.dumps(list(history), indent=2))
        show('p $presenter->paint_context_')
        show('p $presenter->paint_context_.submission_tracker')
        show('p $presenter->paint_context_.present_submission_tracker')
        show('p $presenter->ui_submission_tracker_')
        for member in ('device_lost_', 'submission_open_', 'submission_completed_',
                       'submissions_in_flight_fences_', 'current_submission_wait_semaphores_',
                       'command_buffers_submitted_', 'frame_current_'):
            show('p $xenos_cp->'+member)
        show('bt full')
        show('p *$validation_data')
        show('thread apply all bt 20')
        for thread in gdb.selected_inferior().threads():
            thread.switch()
            frame = gdb.newest_frame()
            while frame:
                name = frame.name() or ''
                if 'VulkanPresenter::PaintAndPresentImpl' in name:
                    frame.select()
                    show('info locals')
                    for expression in ('submit_info', '*submit_info.pWaitSemaphores',
                        '*submit_info.pSignalSemaphores',
                        '*submit_info.pCommandBuffers@submit_info.commandBufferCount',
                        'fence_acqusition', 'paint_submission', 'this->paint_context_',
                        'this->ui_submission_tracker_'):
                        show('p '+expression)
                    break
                if 'CommandProcessor::' in name:
                    frame.select()
                    try:
                        ptr = frame.read_var('this').cast(gdb.lookup_type(
                            'rex::graphics::vulkan::VulkanCommandProcessor').pointer())
                        gdb.set_convenience_variable('xenos_cp', ptr)
                        for member in ('device_lost_', 'submission_open_', 'submission_completed_',
                            'submissions_in_flight_fences_', 'current_submission_wait_semaphores_',
                            'current_submission_wait_stage_masks_', 'command_buffers_submitted_',
                            'command_buffers_writable_', 'frame_current_'):
                            show('p $xenos_cp->'+member)
                        show('info locals')
                        break
                    except (gdb.error, ValueError):
                        pass
                frame = frame.older()
        if original:
            original.switch()
        gdb.write('\nCAPTURE COMPLETE; inferior remains stopped for inspection.\n')


ValidationError('rex::ui::vulkan::VulkanInstance::DebugUtilsMessengerCallback')
RememberObject('rex::ui::vulkan::VulkanPresenter::PaintAndPresentImpl', 'presenter',
               'rex::ui::vulkan::VulkanPresenter')
RememberObject('rex::graphics::CommandProcessor::WorkerThreadMain', 'xenos_cp',
               'rex::graphics::vulkan::VulkanCommandProcessor')
Capture()
