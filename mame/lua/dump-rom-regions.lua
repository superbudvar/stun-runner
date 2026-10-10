-- Dump MAME's loaded static memory regions as raw bytes.
-- Run with STUNRUN_ROM_REGION_DIR set and -autoboot_script pointing here.
-- These are MAME region offsets, not CPU-visible addresses or runtime RAM.

local outdir = assert(os.getenv("STUNRUN_ROM_REGION_DIR"),
                      "STUNRUN_ROM_REGION_DIR must name an existing output directory")
local machine = manager.machine

local function safe_name(name)
    return tostring(name):gsub("[^%w_.-]", "_")
end

local records = {}
for name, region in pairs(machine.memory.regions) do
    local size = region.size
    local path = outdir .. "/" .. safe_name(name) .. ".bin"
    local file = assert(io.open(path, "wb"))
    local chunk = {}

    for offset = 0, size - 1 do
        chunk[#chunk + 1] = region:read_u8(offset)
        if #chunk == 2048 then
            file:write(string.char(table.unpack(chunk)))
            chunk = {}
        end
    end
    if #chunk > 0 then
        file:write(string.char(table.unpack(chunk)))
    end
    file:close()

    local verify = assert(io.open(path, "rb"))
    local actual_size = verify:seek("end")
    verify:close()
    assert(actual_size == size,
           string.format("%s: wrote %d of %d bytes", name, actual_size, size))

    records[#records + 1] = string.format("%s\t%d\t%s", name, size, path)
end

table.sort(records)
for _, record in ipairs(records) do
    print("ROM_REGION_DUMP\t" .. record)
end
machine:exit()
