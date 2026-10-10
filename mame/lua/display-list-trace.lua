-- Bounded call-level trace for the 68010 display-list compiler at 0x225E4.

local machine = manager.machine
local output = assert(os.getenv('STUNRUN_GSP_DL_TRACE_OUT'),
    'STUNRUN_GSP_DL_TRACE_OUT is required')
local limit = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_FRAMES') or '1800')
local capture_start = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_START') or '1')
local capture_end = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_END') or tostring(limit))
local input_mode = os.getenv('STUNRUN_GSP_DL_TRACE_INPUT') or 'drive'
local max_calls = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_MAX_CALLS') or '256')
local raw_bytes = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_RAW_BYTES') or '1024')
local max_fifo_words = tonumber(os.getenv('STUNRUN_GSP_DL_TRACE_MAX_FIFO') or '16384')
local frame = 0
local calls = {}
local active_call
local candidate
local execution_tap
local fifo_count = 0
local call_tap
local fifo_tap

local sw_off_prefix = {
    {port = ':mainpcb:SW1', field = 'SW1:1', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:2', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:3', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:4', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:5', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:6', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:7', value = 1},
    {port = ':mainpcb:SW1', field = 'SW1:8', value = 1}
}
local events = input_mode == 'drive' and {
    {frame = 120, port = ':mainpcb:IN0', field = 'Coin 1', value = 0},
    {frame = 122, port = ':mainpcb:IN0', field = 'Coin 1', value = 1},
    {frame = 300, port = ':mainpcb:a80000', field = '1 Player Start', value = 0},
    {frame = 302, port = ':mainpcb:a80000', field = '1 Player Start', value = 1},
    {frame = 600, port = ':mainpcb:a80000', field = 'P1 Button 1', value = 0},
    {frame = 600, port = ':mainpcb:8BADC.0', field = 'AD Stick X', value = 220},
    {frame = 1200, port = ':mainpcb:a80000', field = 'P1 Button 1', value = 1},
    {frame = 1200, port = ':mainpcb:8BADC.0', field = 'AD Stick X', value = 128}
} or {}
local next_event = 1

local function apply_event(event)
    local port = assert(machine.ioport.ports[event.port], 'unknown input port: ' .. event.port)
    local field = assert(port.fields[event.field],
        'unknown input field: ' .. event.port .. '/' .. event.field)
    field:set_value(event.value)
end

if input_mode == 'drive' then
    emu.register_prestart(function()
        for _, event in ipairs(sw_off_prefix) do apply_event(event) end
    end)
end

local function raw_hex(space, address)
    local bytes = {}
    for index = 0, raw_bytes - 1 do
        bytes[#bytes + 1] = string.format('%02X', space:read_u8(address + index))
    end
    return table.concat(bytes)
end

local function infer_callsite(space, return_address)
    local opcode

    -- JSR absolute-long has a six-byte encoding.
    opcode = space:read_u16(return_address - 6)
    if opcode == 0x4eb9 then
        return return_address - 6, 6, opcode
    end

    -- The d16/indexed/absolute-word/PC-relative JSR forms are four bytes.
    opcode = space:read_u16(return_address - 4)
    if (opcode & 0xffc0) == 0x4e80 then
        local mode = (opcode >> 3) & 7
        local register = opcode & 7
        if mode == 5 or mode == 6 or
            (mode == 7 and (register == 0 or register == 2 or register == 3)) then
            return return_address - 4, 4, opcode
        end
    end
    if opcode == 0x6100 then
        return return_address - 4, 4, opcode
    end

    -- JSR (An), plus the two-byte BSR form.
    opcode = space:read_u16(return_address - 2)
    if (opcode & 0xfff8) == 0x4e90 or
        ((opcode & 0xff00) == 0x6100 and (opcode & 0xff) ~= 0) then
        return return_address - 2, 2, opcode
    end
    return return_address, 0, 0
end

local function install_taps()
    local cpu = assert(machine.devices[':mainpcb:maincpu'], 'main CPU unavailable')
    local space = assert(cpu.spaces['program'], 'main CPU program space unavailable')
    local pc = cpu.state['CURPC'] or cpu.state['PC']
    local sp = assert(cpu.state['A7'], 'main CPU A7 state unavailable')

    -- At function entry the return address is (A7), and the pushed stream
    -- pointer is 4(A7). Runtime code also reaches this routine via JSR (An), so
    -- classify the instruction preceding the return address instead of blindly
    -- subtracting the six-byte length of JSR absolute-long.
    call_tap = space:install_read_tap(0x225e4, 0x225e5,
        'stunrun_gsp_display_list_entry', function()
            if frame < capture_start or frame > capture_end or #calls >= max_calls then
                candidate = nil
                active_call = nil
                return
            end
            active_call = nil
            local stack = sp.value
            local return_address = space:read_u32(stack)
            local stream = space:read_u32(stack + 4)
            local caller, call_size, call_opcode = infer_callsite(space, return_address)
            local call = {
                id = #calls + 1,
                frame = frame,
                return_address = return_address,
                caller = caller & 0x00ffffff,
                call_size = call_size,
                call_opcode = call_opcode,
                stream = stream,
                raw = raw_hex(space, stream),
                fifo = {}
            }
            candidate = call
        end)

    -- A fetch/prefetch alone is not an execution. The opcode loop writes the
    -- watchdog at 0x22608; promote the captured entry only at that observable
    -- effect. Nonzero-entry early exits have no opcode loop and are excluded.
    execution_tap = space:install_write_tap(0x608000, 0x608001,
        'stunrun_gsp_display_list_execution', function()
            if pc.value ~= 0x22608 or candidate == nil then return end
            active_call = candidate
            candidate = nil
            active_call.host_hi = space:read_u16(0xc00000)
            active_call.host_lo = space:read_u16(0xc00004)
            active_call.host_control = space:read_u16(0xc00008)
            calls[#calls + 1] = active_call
        end)

    fifo_tap = space:install_write_tap(0xc0000c, 0xc0000f,
        'stunrun_gsp_display_list_fifo', function(offset, data, mask)
            local pc_value = pc.value
            if frame < capture_start or frame > capture_end or
                active_call == nil or fifo_count >= max_fifo_words or
                pc_value < 0x225e4 or pc_value > 0x22748 then return end
            active_call.fifo[#active_call.fifo + 1] = {
                frame = frame, pc = pc_value, address = offset, data = data, mask = mask,
                destination = (space:read_u16(0xc00000) << 16) |
                    space:read_u16(0xc00004)
            }
            fifo_count = fifo_count + 1
        end)
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame == 10 then install_taps() end
    while next_event <= #events and events[next_event].frame == frame do
        apply_event(events[next_event])
        next_event = next_event + 1
    end
    if frame >= limit then
        for _, call in ipairs(calls) do
            print(string.format(
                'M1_GSP_DL_CALL id=%d frame=%d return=%08X caller=%08X call_size=%d call_opcode=%04X stream=%08X raw=%s',
                call.id, call.frame, call.return_address, call.caller,
                call.call_size, call.call_opcode, call.stream, call.raw))
            print(string.format('M1_GSP_DL_HOST id=%d hi=%04X lo=%04X control=%04X',
                call.id, call.host_hi, call.host_lo, call.host_control))
            for _, word in ipairs(call.fifo) do
                print(string.format(
                    'M1_GSP_DL_FIFO id=%d frame=%d pc=%08X addr=%08X data=%08X mask=%08X destination=%08X',
                    call.id, word.frame, word.pc, word.address, word.data, word.mask,
                    word.destination))
            end
        end
        print(string.format(
            'M1_GSP_DL_DONE frames=%d calls=%d fifo_words=%d calls_truncated=%s fifo_truncated=%s output=%s',
            frame, #calls, fifo_count, tostring(#calls >= max_calls),
            tostring(fifo_count >= max_fifo_words), output))
        machine:exit()
    end
end)
