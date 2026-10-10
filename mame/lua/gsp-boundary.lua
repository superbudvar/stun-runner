-- Bounded 68010 tap for the observed GSP FIFO sink.

local output = assert(os.getenv('STUNRUN_GSP_BOUNDARY_OUT'), 'STUNRUN_GSP_BOUNDARY_OUT is required')
local limit = tonumber(os.getenv('STUNRUN_GSP_BOUNDARY_FRAMES') or '600')
local input_mode = os.getenv('STUNRUN_GSP_BOUNDARY_INPUT') or 'none'
local frame = 0
local capture_start_frame = tonumber(os.getenv('STUNRUN_GSP_BOUNDARY_CAPTURE_START') or '132')
local first_limit = 32
local first = {}
local command_first = {}
local execution_first = {}
local gsp_pc_samples = {}
local reset_writes = {}
local sw_off_prefix = {
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:1', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:2', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:3', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:4', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:5', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:6', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:7', action = 'set', value = 1},
    {frame = 1, port = ':mainpcb:SW1', field = 'SW1:8', action = 'set', value = 1}
}
local events = input_mode == 'drive' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'set', value = 0},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'set', value = 1},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'set', value = 0},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'set', value = 1},
    {frame = 600, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'set', value = 0},
    {frame = 600, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 220},
    {frame = 1200, port = ':mainpcb:a80000', field = 'P1 Button 1', action = 'set', value = 1},
    {frame = 1200, port = ':mainpcb:8BADC.0', field = 'AD Stick X', action = 'set', value = 128}
} or input_mode == 'coin_start' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'}
} or input_mode == 'sw_off' and {
    sw_off_prefix[1], sw_off_prefix[2], sw_off_prefix[3], sw_off_prefix[4],
    sw_off_prefix[5], sw_off_prefix[6], sw_off_prefix[7], sw_off_prefix[8],
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'}
} or input_mode == 'sw_off_prestart' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', action = 'press'},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', action = 'release'},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', action = 'press'},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', action = 'release'}
} or input_mode == 'sw_off_none' and sw_off_prefix or {}
local next_event = 1

local function apply_event(event)
    local port = manager.machine.ioport.ports[event.port]
    assert(port ~= nil, 'unknown input port: ' .. event.port)
    local field = port.fields[event.field]
    assert(field ~= nil, 'unknown input field: ' .. event.port .. '/' .. event.field)
    if event.action == 'press' then field:set_value(1) elseif event.action == 'release' then field:set_value(0) else field:set_value(event.value) end
    print('M1_GSP_INPUT frame=' .. frame .. ' field=' .. event.field .. ' action=' .. event.action)
end

if input_mode == 'sw_off_prestart' or input_mode == 'drive' then
    emu.register_prestart(function()
        for _, event in ipairs(sw_off_prefix) do
            apply_event(event)
        end
    end)
end

local function install_tap()
    local cpu = manager.machine.devices[':mainpcb:maincpu']
    local space = cpu.spaces['program']
    local pc = cpu.state['CURPC']
    local gsp = manager.machine.devices[':mainpcb:gsp']
    local gsp_space = gsp and gsp.spaces['program']
    -- The installed MAME Lua bridge is not stable under a broad, high-rate
    -- device-window tap. The FIFO subrange is the proven stable boundary and
    -- is sufficient to identify runtime submission PCs and ordering.
    local function record_command(offset, data, mask)
        if frame < capture_start_frame or #command_first >= first_limit then return end
        command_first[#command_first + 1] = {
            frame = frame, pc = pc.value, address = offset, data = data, mask = mask,
            post_init = pc.value > 0x4f4
        }
    end
    space:install_write_tap(0xc00002, 0xc00003, 'stunrun_gsp_command', record_command)
    space:install_write_tap(0xc0000c, 0xc0000f, 'stunrun_gsp_fifo',
        function(offset, data, mask)
            if frame < capture_start_frame or #first >= first_limit then return end
            local pc_value = pc.value
            first[#first + 1] = {
                frame = frame, pc = pc_value, address = offset, data = data, mask = mask,
                post_init = pc_value > 0x4f4
            }
        end)
    space:install_write_tap(0x604000, 0x607fff, 'stunrun_gsp_reset_control',
        function(offset, data, mask)
            if #reset_writes < first_limit then
                reset_writes[#reset_writes + 1] = {
                    frame = frame, address = offset, data = data, mask = mask
                }
            end
        end)
    -- Host FIFO writes are only one possible submission path.  Record
    -- execution of the known GSP startup/consumer landmarks independently so
    -- an empty host-port tap cannot be mistaken for an idle GSP.
    if gsp_space ~= nil then
        gsp_space:install_execute_tap(0xfff59920, 0xfff59920,
            'stunrun_gsp_startup_landmark', function()
                if frame >= capture_start_frame and #execution_first < first_limit then
                    execution_first[#execution_first + 1] = {frame = frame, pc = 0xfff59920}
                end
            end)
        gsp_space:install_execute_tap(0xfff48c20, 0xfff48c20,
            'stunrun_gsp_fifo_consumer_landmark', function()
                if frame >= capture_start_frame and #execution_first < first_limit then
                    execution_first[#execution_first + 1] = {frame = frame, pc = 0xfff48c20}
                end
            end)
    end
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame == 10 then install_tap() end
    local sample_frame = frame == capture_start_frame or frame == 60 or
        frame == 120 or frame == 122 or frame == 132 or frame == 136 or
        frame == 180 or frame == limit
    if sample_frame then
        local gsp = manager.machine.devices[':mainpcb:gsp']
        local state = gsp and gsp.state['CURPC']
        if state ~= nil then
            gsp_pc_samples[#gsp_pc_samples + 1] = {frame = frame, pc = state.value}
        end
    end
    while next_event <= #events and events[next_event].frame == frame do
        apply_event(events[next_event])
        next_event = next_event + 1
    end
    if frame >= limit then
        for _, row in ipairs(first) do
            print(string.format('M1_GSP_WRITE_FIRST frame=%d pc=%08X addr=%08X data=%08X mask=%08X post_init=%s',
                row.frame, row.pc, row.address, row.data, row.mask, tostring(row.post_init)))
        end
        for _, row in ipairs(command_first) do
            print(string.format('M1_GSP_COMMAND_FIRST frame=%d pc=%08X addr=%08X data=%08X mask=%08X post_init=%s',
                row.frame, row.pc, row.address, row.data, row.mask, tostring(row.post_init)))
        end
        for _, row in ipairs(execution_first) do
            print(string.format('M1_GSP_EXEC_FIRST frame=%d pc=%08X', row.frame, row.pc))
        end
        for _, row in ipairs(gsp_pc_samples) do
            print(string.format('M1_GSP_PC_SAMPLE frame=%d pc=%08X', row.frame, row.pc))
        end
        for _, row in ipairs(reset_writes) do
            print(string.format('M1_GSP_RESET_WRITE frame=%d addr=%08X data=%08X mask=%08X',
                row.frame, row.address, row.data, row.mask))
        end
        print('M1_GSP_BOUNDARY_DONE frames=' .. frame .. ' output=' .. output)
        manager.machine:exit()
    end
end)
