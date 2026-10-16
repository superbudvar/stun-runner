-- Bounded same-run ADSP SOMLATCH->GINT capture plus the 68010 ferry afterward.
-- Input is deliberately fixed to none; this avoids scripted-input evidence arms.

local machine = manager.machine
local first_frame = tonumber(os.getenv('STUNRUN_ADSP_TX_FIRST') or '1')
local last_frame = tonumber(os.getenv('STUNRUN_ADSP_TX_LAST') or '600')
local post_frames = tonumber(os.getenv('STUNRUN_ADSP_TX_POST_FRAMES') or '5')
local max_events = tonumber(os.getenv('STUNRUN_ADSP_TX_MAX_EVENTS') or '50000')
local dm_window_base = tonumber(os.getenv('STUNRUN_ADSP_TX_DM_BASE') or '0x30')
local dm_window_limit = tonumber(os.getenv('STUNRUN_ADSP_TX_DM_LIMIT') or '0x60')
local record_index = 0
local dumping_state = false -- guards re-entry: reading the tapped
    -- space from inside its own tap calls this handler again

local frame = 0
local sequence = 0
local events = 0
local truncated = false
local installed = false
local host_bank = 0 -- harddriv_state::device_reset initializes this to 0
local bank_events = 0
local tx_active = false
local tx_done = false
local tx_start_frame = nil
local tx_start_seq = nil
local tx_end_frame = nil
local tx_end_seq = nil
local taps = {} -- retain the tap objects; Lua GC otherwise removes them
local snapshot_regs = {
    'AR', 'AF', 'AX0', 'AX1', 'AY0', 'AY1', 'MY0', 'MR0', 'MR1', 'MR2',
    'SR', 'SI', 'SE', 'I0', 'I1', 'I2', 'I3', 'I4', 'I5', 'I6', 'I7',
    'M0', 'M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7',
    'L0', 'L1', 'L2', 'L3', 'L4', 'L5', 'L6', 'L7', 'CNTR',
}

local function snapshot_suffix(device, pc)
    if not ((pc >= 0x0514 and pc <= 0x05d4) or
            (pc >= 0x05dd and pc <= 0x0630)) then return '' end
    local parts = {}
    for _, name in ipairs(snapshot_regs) do
        local ok, entry = pcall(function() return device.state[name] end)
        if ok and entry ~= nil then
            local okv, value = pcall(function() return entry.value end)
            if okv and type(value) == 'number' then
                parts[#parts + 1] = name .. ':' .. string.format('%X', value)
            end
        end
    end
    return #parts > 0 and (' regs=' .. table.concat(parts, ',')) or ''
end

local function emit(cpu, access, address, data, mask, pc, device)
    sequence = sequence + 1
    if frame < first_frame or frame > last_frame then return end

    local is_adsp = cpu == 'adsp' or cpu == 'adsp_program'
    if cpu == 'adsp' and access == 'W' and address == 0x2003 and not tx_active and not tx_done then
        tx_active = true
        tx_start_frame = frame
        tx_start_seq = sequence
    end

    local include = false
    if is_adsp and tx_active then
        include = true
    elseif cpu == 'maincpu' and tx_start_seq ~= nil then
        local post_end = tx_end_frame and (tx_end_frame + post_frames) or last_frame
        if frame <= post_end then
            include = (access == 'W' and
                       ((address >= 0x818000 and address <= 0x81807f) or
                        (address >= 0x00c0000c and address <= 0x00c0000f))) or
                      (access == 'R' and address >= 0x810000 and address <= 0x813fff)
        end
    end

    if include then
        if events >= max_events then
            truncated = true
            return
        end
        events = events + 1
        print(string.format(
            'M1_ADSP_TX_EVENT seq=%d frame=%d cpu=%s access=%s address=%08X data=%08X mask=%08X pc=%08X bank=%d%s',
            sequence, frame, cpu, access, address, data, mask, pc, host_bank,
            device and snapshot_suffix(device, pc) or ''))
    end

    if cpu == 'adsp' and tx_active and access == 'W' and address == 0x2006 then
        tx_active = false
        tx_done = true
        tx_end_frame = frame
        tx_end_seq = sequence
    end
end

local function install_taps()
    local adsp = assert(machine.devices[':mainpcb:adsp'], 'ADSP missing')
    local dm = assert(adsp.spaces['data'], 'ADSP data space missing')
    local adsp_pc = adsp.state['CURPC'] or adsp.state['PC']

    taps[#taps + 1] = dm:install_read_tap(0x0000, 0x2fff,
        'stunrun_adsp_tx_dm_read', function(address, data, mask)
            -- The window dump below reads this same space, which re-enters this
            -- tap. Suppress everything while it runs, or the dump's own reads
            -- are recorded as ADSP traffic and corrupt the event stream.
            if dumping_state then return end
            -- $0564 is the first read of the record emitter's output path and
            -- is the point where both header pointers are settled. Capture the
            -- data-memory window there so the walk can be replayed offline.
            if tx_active and adsp_pc.value == 0x0564 and not dumping_state then
                dumping_state = true
                record_index = record_index + 1
                local words = {}
                for a = dm_window_base, dm_window_limit - 1 do
                    words[#words + 1] = string.format('%04X', dm:read_u16(a))
                end
                local i1 = 0; local i2 = 0; local m1 = 0; local m2 = 0
                local ok1, e1 = pcall(function() return adsp.state['I1'].value end)
                if ok1 and type(e1) == 'number' then i1 = e1 end
                local ok2, e2 = pcall(function() return adsp.state['I2'].value end)
                if ok2 and type(e2) == 'number' then i2 = e2 end
                local ok3, e3 = pcall(function() return adsp.state['M1'].value end)
                if ok3 and type(e3) == 'number' then m1 = e3 end
                local ok4, e4 = pcall(function() return adsp.state['M2'].value end)
                if ok4 and type(e4) == 'number' then m2 = e4 end
                print(string.format(
                    'M1_ADSP_TX_STATE record=%d frame=%d i1=%d i2=%d m1=%d m2=%d dm=%04X..%04X words=%s',
                    record_index, frame, i1, i2, m1, m2, dm_window_base,
                    dm_window_limit - 1, table.concat(words, ',')))
                dumping_state = false
            end
            emit('adsp', 'R', address, data, mask, adsp_pc.value, adsp)
        end)
    taps[#taps + 1] = dm:install_write_tap(0x0000, 0x2fff,
        'stunrun_adsp_tx_dm_write', function(address, data, mask)
            emit('adsp', 'W', address, data, mask, adsp_pc.value, adsp)
        end)
    local adsp_program = assert(adsp.spaces['program'], 'ADSP program space missing')
    taps[#taps + 1] = adsp_program:install_read_tap(0x1000, 0x1fff,
        'stunrun_adsp_tx_pm_lookup', function(address, data, mask)
            -- The helper indexes PM at $1036+M6. Capture the table window
            -- rather than keying on PC; CURPC can point past the current op.
            emit('adsp_program', 'R', address, data, mask, adsp_pc.value, adsp)
        end)

    local maincpu = assert(machine.devices[':mainpcb:maincpu'], 'main CPU missing')
    local program = assert(maincpu.spaces['program'], 'main CPU program space missing')
    local main_pc = maincpu.state['CURPC'] or maincpu.state['PC']
    taps[#taps + 1] = program:install_write_tap(0x818000, 0x81807f,
        'stunrun_adsp_tx_control', function(address, data, mask)
            -- Main CPU program-space offsets are byte addresses; the MAME
            -- 16-bit handler receives the word offset.
            local offset = math.floor((address - 0x818000) / 2)
            if (offset & 7) == 3 then
                host_bank = (offset >> 3) & 1
                bank_events = bank_events + 1
                print(string.format(
                    'M1_ADSP_TX_BANK seq=%d frame=%d address=%08X offset=%X bank=%d pc=%08X',
                    sequence + 1, frame, address, offset, host_bank, main_pc.value))
            end
            emit('maincpu', 'W', address, data, mask, main_pc.value)
        end)
    taps[#taps + 1] = program:install_read_tap(0x810000, 0x813fff,
        'stunrun_adsp_tx_buffer_read', function(address, data, mask)
            emit('maincpu', 'R', address, data, mask, main_pc.value)
        end)
    taps[#taps + 1] = program:install_write_tap(0x00c0000c, 0x00c0000f,
        'stunrun_adsp_tx_hstdat', function(address, data, mask)
            emit('maincpu', 'W', address, data, mask, main_pc.value)
        end)

    installed = true
    print('M1_ADSP_TX_TAPS_INSTALLED frame=' .. frame)
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame == 1 and not installed then install_taps() end
    if frame >= last_frame then
        print(string.format(
            'M1_ADSP_TX_DONE frames=%d events=%d truncated=%s bank_events=%d tx_start_frame=%s tx_start_seq=%s tx_end_frame=%s tx_end_seq=%s records=%d',
            frame, events, tostring(truncated), bank_events,
            tostring(tx_start_frame), tostring(tx_start_seq),
            tostring(tx_end_frame), tostring(tx_end_seq), record_index))
        machine:exit()
    end
end)
