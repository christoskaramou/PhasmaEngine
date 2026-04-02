-- Filesystem binding test suite

function run_filesystem_tests()
    pe_log("=== Filesystem Tests ===")
    T.reset()

    -- find files
    local results = fs.find(".lua")
    T.check("find returns table", type(results) == "table")
    T.check("find .lua has results", #results > 0)

    local gltf = fs.find(".gltf")
    T.check("find .gltf returns table", type(gltf) == "table")

    -- find with empty query
    local empty = fs.find("")
    T.check("find empty returns empty table", type(empty) == "table" and #empty == 0)

    -- list directory
    local listing = fs.list(assets_path .. "Scripts")
    T.check("list returns table", type(listing) == "table")
    T.check("list has files", type(listing.files) == "table")
    T.check("list has dirs", type(listing.dirs) == "table")

    -- list nonexistent
    local bad = fs.list("/nonexistent_path_12345")
    T.check("list bad path returns empty", type(bad) == "table" and bad.files == nil)

    -- read file
    local content = fs.read(assets_path .. "Scripts/material_tweaker.lua")
    T.check("read returns string", type(content) == "string")
    T.check("read non-empty", content ~= nil and #content > 0)

    -- read nonexistent
    local bad2 = fs.read("nonexistent_file_12345.txt")
    T.check("read nonexistent returns nil", bad2 == nil)

    -- write and read back
    local testPath = assets_path .. "Agent/test_fs_binding.tmp"
    local ok = fs.write(testPath, "hello from lua")
    T.check("write returns true", ok == true)

    local readBack = fs.read(testPath)
    T.check("read back matches", readBack == "hello from lua")

    -- append
    local ok2 = fs.write(testPath, " appended", true)
    T.check("append returns true", ok2 == true)

    local readBack2 = fs.read(testPath)
    T.check("append works", readBack2 == "hello from lua appended")

    -- cleanup
    os.remove(testPath)

    T.summary("Filesystem Tests")
end
