-- Bounded same-run ADSP SOMLATCH->GINT capture plus the 68010 ferry afterward.
-- Input is deliberately fixed to none; this avoids scripted-input evidence arms.

local machine = manager.machine
local first_frame = tonumber(os.getenv('STUNRUN_ADSP_TX_FIRST') or '1')
local last_frame = tonumber(os.getenv('STUNRUN_ADSP_TX_LAST') or '600')
local post_frames = tonumber(os.getenv('STUNRUN_ADSP_TX_POST_FRAMES') or '5')
local max_events = tonumber(os.getenv('STUNRUN_ADSP_TX_MAX_EVENTS') or '50000')

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
    'SR', 'SI', 'SE', 'I0', 'I1', 'I2', 'I3', 'I4', 'I5', 'I6',
    'M0', 'M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'L0', 'L1', 'L2', 'L3',
}

local function snapshot_suffix(device, pc)
    if not ((pc >= 0x0570 and pc <= 0x0580) or
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
            'M1_ADSP_TX_DONE frames=%d events=%d truncated=%s bank_events=%d tx_start_frame=%s tx_start_seq=%s tx_end_frame=%s tx_end_seq=%s',
            frame, events, tostring(truncated), bank_events,
            tostring(tx_start_frame), tostring(tx_start_seq),
            tostring(tx_end_frame), tostring(tx_end_seq)))
        machine:exit()
    end
end)
