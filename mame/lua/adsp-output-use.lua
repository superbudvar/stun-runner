-- Ordered, bounded ADSP output / banked SOM / host / GSP operand observation.
local machine = manager.machine
local first = tonumber(os.getenv('STUNRUN_OUTPUT_FIRST') or '540')
local last = tonumber(os.getenv('STUNRUN_OUTPUT_LAST') or '620')
local cap = tonumber(os.getenv('STUNRUN_OUTPUT_CAP') or '400000')
local handle = assert(io.open(assert(os.getenv('STUNRUN_OUTPUT_LOG')), 'w'))
local frame, sequence, count = 0, 0, 0
local bank, somaddr, truncated = 0, 0, false
local taps = {}
local last_fill
local pen = 0
local function emit(kind, pc, addr, data, mask, extra)
    if frame < first or frame > last then return end
    sequence = sequence + 1
    if count >= cap then truncated = true; return end
    count = count + 1
    handle:write(string.format('%d %d %s %08X %08X %08X %08X %d %s\n',
        sequence, frame, kind, pc, addr, data, mask, bank, extra or '-'))
end
local function install()
    local main = assert(machine.devices[':mainpcb:maincpu'])
    local adsp = assert(machine.devices[':mainpcb:adsp'])
    local gsp = assert(machine.devices[':mainpcb:gsp'])
    local ms, ds, gs = main.spaces['program'], adsp.spaces['data'], gsp.spaces['program']
    local mp, dp, gp = main.state['CURPC'], adsp.state['CURPC'], gsp.state['CURPC']
    taps[#taps+1] = gs:install_write_tap(0xf4000000,0xf400001f,'output_pen',function(a,d,m)
        if a == 0xf4000000 then pen = ((pen & (~m)) | (d & m)) & 0xffff end
        emit('colour',gp.value,a,d,m)
    end)
    taps[#taps+1] = ds:install_write_tap(0x2000, 0x2007, 'output_adsp', function(a,d,m)
        if a == 0x2003 then somaddr = d & 0x1fff end
        if a == 0x2002 then
            emit('som',dp.value,somaddr,d,m,tostring(bank ~ 1))
            somaddr = (somaddr+1) & 0x1fff
        else emit('port',dp.value,a,d,m) end
    end)
    taps[#taps+1] = ms:install_write_tap(0x818000,0x81807f,'output_bank',function(a,d,m)
        local off = (a-0x818000)//2
        if (off & 7) == 3 then bank = (off >> 3) & 1 end
        emit('control',mp.value,a,d,m)
    end)
    taps[#taps+1] = ms:install_read_tap(0x810000,0x813fff,'output_som_read',function(a,d,m)
        emit('read',mp.value,a,d,m)
    end)
    taps[#taps+1] = ms:install_write_tap(0xc00000,0xc0000f,'output_host',function(a,d,m)
        local dest = ((ms:read_u16(0xc00000)<<16)|ms:read_u16(0xc00004)) & 0xffffffff
        emit('host',mp.value,a,d,m,string.format('%08X',dest))
    end)
    taps[#taps+1] = gs:install_read_tap(0xff800000,0xffffffff,'output_gsp_read',function(a,d,m)
        local pc = gp.value
        if pc >= 0xfff44bf0 and pc < 0xfff46000 and
           not (a >= 0xfff44000 and a < 0xfff49000) then
            local names = {'A1','A3','A5','A8','A10','A11','A12','A13','B0','B2','B7','B9'}
            local values = {}
            for _,n in ipairs(names) do values[#values+1]=string.format('%s:%08X',n,gsp.state[n].value & 0xffffffff) end
            emit('gsp',pc,a,d,m,table.concat(values,','))
        end
    end)
    taps[#taps+1] = gs:install_write_tap(0x02000000,0x020fffff,'output_gsp_fill',function(a,d,m)
        local pc = gp.value
        if pc >= 0xfff45090 and pc < 0xfff46000 then
            local values = {}
            for _,n in ipairs({'A4','A6','A0','A5','A7','A8','A9','A10','B0','B1','B2','B7','B9','B4'}) do
                values[#values+1]=string.format('%s:%08X',n,gsp.state[n].value & 0xffffffff)
            end
            values[#values+1] = string.format('PEN:%04X',pen)
            local key = string.format('%X:%X:%X',pc,gsp.state['B0'].value,gsp.state['A6'].value)
            if key ~= last_fill then
                emit('fill',pc,a,d,m,table.concat(values,','))
                last_fill = key
            end
        else last_fill = nil end
    end)
    print('ADSP_OUTPUT_INSTALLED')
end
emu.register_frame_done(function()
    frame = frame+1
    if frame == 1 then install() end
    if frame >= last then
        handle:close()
        print(string.format('ADSP_OUTPUT_DONE frames=%d events=%d truncated=%s',frame,count,tostring(truncated)))
        machine:exit()
    end
end)
