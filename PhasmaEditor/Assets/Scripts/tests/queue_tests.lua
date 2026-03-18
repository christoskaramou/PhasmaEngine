-- Queue API test suite (called from main.lua)

function run_queue_tests()
    pe_log("=== Queue API Tests ===")
    T.reset()

    -- Get the main queue
    local queue = rhi.get_main_queue()
    T.check("get_main_queue", queue ~= nil)

    -- GetFamilyId
    T.check("get_family_id", queue.get_family_id ~= nil)

    -- GetSubmissionCount
    local count_before = queue.get_submission_count
    T.check("get_submission_count", count_before ~= nil)

    -- GetSubmissionsSemaphore
    local sem = queue:get_submissions_semaphore()
    T.check("get_submissions_semaphore", sem ~= nil)

    -- AcquireCommandBuffer
    local cmd = queue:acquire_command_buffer()
    T.check("acquire_command_buffer", cmd ~= nil)

    -- Submit (single cmd, no semaphores)
    cmd:begin()
    cmd:end_cmd()
    queue:submit(cmd)
    T.check("submit", true)

    -- Wait
    queue:wait()
    T.check("wait", true)

    -- GetSubmissionCount (should have increased)
    local count_after = queue.get_submission_count
    T.check("submission_count increased", count_after > count_before)

    -- ReturnCommandBuffer
    cmd:wait()
    cmd:return_cmd()
    T.check("return_command_buffer via cmd", true)

    -- BeginDebugRegion / InsertDebugLabel / EndDebugRegion
    queue:begin_debug_region("lua_queue_test")
    T.check("begin_debug_region", true)
    queue:insert_debug_label("lua_queue_label")
    T.check("insert_debug_label", true)
    queue:end_debug_region()
    T.check("end_debug_region", true)

    -- WaitIdle
    queue:wait_idle()
    T.check("wait_idle", true)

    -- CommandPool (get from a command buffer)
    local cmd2 = queue:acquire_command_buffer()
    local pool = cmd2:get_command_pool()
    T.check("CommandPool get_command_pool", pool ~= nil)

    -- CommandPool::GetQueue
    local pool_queue = pool:get_queue()
    T.check("CommandPool get_queue", pool_queue ~= nil)

    -- CommandPool::GetFlags
    local flags = pool:get_flags()
    T.check("CommandPool get_flags", flags ~= nil)

    -- Return cmd2
    cmd2:begin()
    cmd2:end_cmd()
    queue:submit(cmd2)
    cmd2:wait()
    cmd2:return_cmd()

    -- CommandPool::Reset - skipped, destructive to engine's internal command buffers

    T.summary("Queue API Tests")
end
