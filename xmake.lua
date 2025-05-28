add_rules("mode.debug", "mode.release")

set_languages("c++20")
set_toolchains("clang")
set_warnings("all", "error", "extra", "pedantic")

if is_mode("debug") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end

-- xmake f -p mingw --mingw="c:\llvm-mingw" -m debug -c --yes
-- xmake project -k compile_commands

target("pbr")
    set_kind("binary")
    add_includedirs("deps/DirectXMath/Inc", "deps/tinyobjloader", "deps/stb", "deps/cgltf", "deps/cjson")
    add_files("src/*.cpp", "deps/cjson/cJSON.c")
    add_syslinks("d3d11", "d3dcompiler")
    add_cxflags("-fno-sanitize=vptr", "-Wno-defaulted-function-deleted")

    if is_mode("debug") then
        add_defines("_DEBUG")
    end
    
    set_rundir(os.projectdir())
