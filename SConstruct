#!/usr/bin/env python
import os
import sys

env = SConscript("godot-cpp/SConstruct")

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/", "lib/LibSWBF2-redux/"])
env.Append(LIBPATH=["lib/"])
env.Append(LIBS=["libSWBF2"]) # https://git.prettyshitty.city/LibSWBF2-redux
sources = Glob("src/*.cpp")
warnings=['-Wall', '-Wextra', '-Wno-attributes', '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-sign-compare']

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/bin/libgdlvlimport.{}.{}.framework/libgdlvlimport.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
        CCFLAGS=warnings
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "demo/bin/libgdlvlimport.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
            CCFLAGS=warnings
        )
    else:
        library = env.StaticLibrary(
            "demo/bin/libgdlvlimport.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
            CCFLAGS=warnings
        )
else:
    library = env.SharedLibrary(
        "demo/bin/libgdlvlimport{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
        CCFLAGS=warnings
    )

Default(library)
