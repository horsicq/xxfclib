# Use the same vendored release as diec, without Qt or the other architectures.
# Objects are embedded in xxfclib's archive so installed static consumers do
# not need a second archive. Upstream source files remain unchanged.
set(XXFC_CAPSTONE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../XCapstone/3rdparty/Capstone/src"
    CACHE PATH "Vendored Capstone source used by the DIE x86 decoder")
option(XXFC_CAPSTONE_X86_REDUCE "Match diec's reduced x86 Capstone configuration" ON)
if(NOT EXISTS "${XXFC_CAPSTONE_ROOT}/cs.c")
    message(FATAL_ERROR "DIE decoder requires Capstone sources; set XXFC_CAPSTONE_ROOT")
endif()
set(_xxfc_capstone_sources
    cs.c utils.c MCInst.c MCInstrDesc.c MCRegisterInfo.c SStream.c
    arch/X86/X86ATTInstPrinter.c arch/X86/X86Disassembler.c
    arch/X86/X86DisassemblerDecoder.c arch/X86/X86InstPrinterCommon.c
    arch/X86/X86IntelInstPrinter.c arch/X86/X86Mapping.c arch/X86/X86Module.c)
list(TRANSFORM _xxfc_capstone_sources PREPEND "${XXFC_CAPSTONE_ROOT}/")
add_library(xxfc_capstone OBJECT ${_xxfc_capstone_sources})
target_include_directories(xxfc_capstone PRIVATE
    "${XXFC_CAPSTONE_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(xxfc_capstone PRIVATE
    CAPSTONE_HAS_X86 CAPSTONE_USE_SYS_DYN_MEM NDEBUG XXFC_STATIC)
if(XXFC_CAPSTONE_X86_REDUCE)
    target_compile_definitions(xxfc_capstone PRIVATE CAPSTONE_X86_REDUCE CAPSTONE_X86_ATT_DISABLE)
endif()
set_target_properties(xxfc_capstone PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(_xxfc_capstone_runtime "${CMAKE_CURRENT_SOURCE_DIR}/src/die_engine/xx_die_engine_capstone_runtime.h")
if(MSVC)
    target_compile_options(xxfc_capstone PRIVATE /W0 "/FI${_xxfc_capstone_runtime}")
    if(XXFC_MSVC_X64)
        target_compile_options(xxfc_capstone PRIVATE /GS- /GL- /Zl)
        set_property(TARGET xxfc_capstone PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        if(POLICY CMP0184)
            set_property(TARGET xxfc_capstone PROPERTY MSVC_RUNTIME_CHECKS "")
        else()
            target_compile_options(xxfc_capstone PRIVATE /RTC-)
        endif()
    endif()
else()
    target_compile_options(xxfc_capstone PRIVATE -w -include "${_xxfc_capstone_runtime}")
endif()
list(APPEND XXFCLIB_SOURCES $<TARGET_OBJECTS:xxfc_capstone>)
