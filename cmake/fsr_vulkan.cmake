# heliasdream-vk: FireBurn/FSR-Vulkan (MIT, third_party/FSR-Vulkan, a git
# submodule pinned in .gitmodules) - AMD's FidelityFX FSR 3 upscaler on native
# Vulkan with its SPIR-V shader tables checked in (generated/). Phase 0 only
# builds and links it: src/host/upscaler.cpp references the FSR 3.1.5 and the
# 3.1.4 upscalers and counts the embedded 3.1.5 shader modules, and nothing
# records an FSR 3 dispatch yet (docs/vk/PLAN.md, Phase 3).
#
# Only what heliasdream links is built (EXCLUDE_FROM_ALL): the portable contract,
# the 3.1.4 Vulkan backend and the 3.1.5 upscaler. Its tests, the capability
# probe, the experimental source-v07 "FSR4" provider (which is not AMD FSR
# 4.1.1; Phase 4 builds that from the player's own DLL instead) and its install
# rules are off. HELIAS_FSR_VULKAN=OFF leaves it out.
option(HELIAS_FSR_VULKAN "link FireBurn/FSR-Vulkan (FSR 3.1) into heliasdream" ON)

set(HELIAS_FSR_VULKAN_DIR ${CMAKE_SOURCE_DIR}/third_party/FSR-Vulkan)
if(HELIAS_FSR_VULKAN AND Vulkan_FOUND)
  if(NOT EXISTS ${HELIAS_FSR_VULKAN_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "heliasdream: third_party/FSR-Vulkan is empty: run `git submodule update --init` "
                        "(or configure with -DHELIAS_FSR_VULKAN=OFF)")
  endif()
  set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_HOST ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_VK_BACKEND ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_3_1_5_SCAFFOLD ON CACHE BOOL "" FORCE)
  # Helia's Dream: the FSR 4 INT8 (v07) provider, AMD's MIT FSR 4 source - its
  # shaders and weights are not built here: the player downloads them
  # (host/fsr4v07_pass.h).
  set(FFX_VK_PORTABLE_BUILD_FSR4_V07_VULKAN ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_INSTALL_FSR4_V07_ASSETS OFF CACHE BOOL "" FORCE)
  # Its static libraries go into a position-independent executable.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(${HELIAS_FSR_VULKAN_DIR} ${CMAKE_BINARY_DIR}/third_party/FSR-Vulkan EXCLUDE_FROM_ALL SYSTEM)
  # Its own -Wall -Wextra -Wpedantic would drown heliasdream's build log; it is
  # third-party code, built as it ships.
  foreach(t ffx_vulkan_portable ffx_vulkan_fsr3_host_1_1_4 ffx_vulkan_fsr3_vk_backend_1_1_4
            ffx_vulkan_fsr3_host_3_1_5_scaffold ffx_vulkan_fsr3_vk_backend_3_1_5 ffx_vulkan_fsr4_v07_vulkan ffx_vulkan_fsr4_v07_assets)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE -w)
      # mingw-w64's headers do not bring in what the SDK's sources use
      # without including (memset, memcmp, swprintf, placement new, log2).
      if(WIN32)
        target_compile_options(${t} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstring>" "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cwchar>"
                               "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include new>" "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cmath>")
      endif()
    endif()
  endforeach()
  # heliasdream-vk Phase 5: cmake/patches/fsr-vulkan-recycle.patch, applied to
  # copies of the three files it touches in the build tree (the submodule
  # stays untouched at its pinned commit), and those copies compiled instead.
  # A patch that no longer applies (a new submodule pin) is said and skipped:
  # the build goes on with the files as they ship.
  option(HELIAS_FSR_VULKAN_PATCHES "apply cmake/patches/fsr-vulkan-*.patch to copies of FSR-Vulkan's sources" ON)
  if(HELIAS_FSR_VULKAN_PATCHES AND TARGET ffx_vulkan_portable AND TARGET ffx_vulkan_fsr3_vk_backend_3_1_5)
    set(_fsrp ${CMAKE_BINARY_DIR}/fsr_vulkan_patched)
    set(_fsrp_patch ${CMAKE_SOURCE_DIR}/cmake/patches/fsr-vulkan-recycle.patch)
    set(_fsrp_files src/ffx_vk_fsr3_3_1_5_bridge.cpp src/ffx_vk_fsr3_3_1_5_descriptor.c include/ffx_vk_fsr3_3_1_5_descriptor.h)
    file(REMOVE_RECURSE ${_fsrp})
    foreach(f ${_fsrp_files})
      get_filename_component(_d ${f} DIRECTORY)
      file(COPY ${HELIAS_FSR_VULKAN_DIR}/${f} DESTINATION ${_fsrp}/${_d})
    endforeach()
    find_program(HELIAS_PATCH_EXE patch)
    set(_fsrp_ok FALSE)
    if(HELIAS_PATCH_EXE)
      execute_process(COMMAND ${HELIAS_PATCH_EXE} -p1 --forward --batch --silent -i ${_fsrp_patch}
                      WORKING_DIRECTORY ${_fsrp} RESULT_VARIABLE _fsrp_rc OUTPUT_VARIABLE _fsrp_out ERROR_VARIABLE _fsrp_out)
      if(_fsrp_rc EQUAL 0)
        set(_fsrp_ok TRUE)
      endif()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_fsrp_patch})
    if(_fsrp_ok)
      foreach(pair "ffx_vulkan_portable|src/ffx_vk_fsr3_3_1_5_descriptor.c" "ffx_vulkan_fsr3_vk_backend_3_1_5|src/ffx_vk_fsr3_3_1_5_bridge.cpp")
        string(REPLACE "|" ";" pair "${pair}")
        list(GET pair 0 _t)
        list(GET pair 1 _f)
        get_target_property(_srcs ${_t} SOURCES)
        list(TRANSFORM _srcs REPLACE "^(.*/)?${_f}$" "${_fsrp}/${_f}")
        set_property(TARGET ${_t} PROPERTY SOURCES ${_srcs})
        target_include_directories(${_t} BEFORE PRIVATE ${_fsrp}/include)
      endforeach()
      message(STATUS "heliasdream: FSR-Vulkan patched in the build tree (cmake/patches/fsr-vulkan-recycle.patch)")
    else()
      message(WARNING "heliasdream: cmake/patches/fsr-vulkan-recycle.patch did not apply (${_fsrp_out}); FSR-Vulkan is built as it ships")
    endif()
  endif()
  target_sources(heliasdream PRIVATE src/host/upscaler_fsr3.cpp src/host/fsr3_upscaler.cpp)
  set_source_files_properties(src/host/fsr3_upscaler.cpp PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
  target_compile_definitions(heliasdream PRIVATE HELIAS_HAVE_FSR_VULKAN=1)
  target_link_libraries(heliasdream PRIVATE ffx-vulkan::portable ffx-vulkan::fsr3-vk-backend-3.1.5)
  if(TARGET ffx_vulkan_fsr4_v07_vulkan)
    target_link_libraries(heliasdream PRIVATE ffx-vulkan::fsr4-v07-vulkan)
    target_compile_definitions(heliasdream PRIVATE HELIAS_HAVE_FSR4_V07=1)
  endif()
  message(STATUS "heliasdream: FSR-Vulkan linked (FSR 3.1.5 and 3.1.4 upscalers, shader tables embedded)")
else()
  message(STATUS "heliasdream: FSR-Vulkan not linked (HELIAS_FSR_VULKAN=${HELIAS_FSR_VULKAN})")
endif()
