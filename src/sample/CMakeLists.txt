PROJECT(sample VERSION 1.0 LANGUAGES CXX)

# Resolve package dependencies.
FIND_PACKAGE(directx-headers CONFIG REQUIRED)
FIND_PACKAGE(directx-agility-sdk CONFIG REQUIRED)
FIND_PACKAGE(d3d12-memory-allocator CONFIG REQUIRED)
FIND_PACKAGE(winpixeventruntime CONFIG REQUIRED)

# Attempt to find the proper dxcompiler.dll file from the Windows SDK and copy it to the runtime directory. This fixes an issue where the wrong (x64) dll
# is picked up from the Vulkan SDK in x86 builds.
IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
    FIND_FILE(DXCOMPILER_DLL "dxcompiler.dll" PATHS "$ENV{WindowsSdkVerBinPath}/x64/" NO_DEFAULT_PATH)
    FIND_FILE(DXIL_DLL "dxil.dll" PATHS "$ENV{WindowsSdkVerBinPath}/x64/" NO_DEFAULT_PATH)
ELSEIF(CMAKE_SIZEOF_VOID_P EQUAL 4)
    FIND_FILE(DXCOMPILER_DLL "dxcompiler.dll" PATHS "$ENV{WindowsSdkVerBinPath}/x86/" NO_DEFAULT_PATH)
    FIND_FILE(DXIL_DLL "dxil.dll" PATHS "$ENV{WindowsSdkVerBinPath}/x86/" NO_DEFAULT_PATH)
ENDIF(CMAKE_SIZEOF_VOID_P EQUAL 8)

IF(NOT DXCOMPILER_DLL OR NOT DXIL_DLL)
    MESSAGE(WARNING "Unable to locate dxcompiler.dll or dxil.dll from Windows SDK. You may have to install the DLL manually.")
ENDIF(NOT DXCOMPILER_DLL OR NOT DXIL_DLL)

ADD_EXECUTABLE(${PROJECT_NAME}
    "main.cxx"
)

# Link project dependencies.
TARGET_LINK_LIBRARIES(${PROJECT_NAME}
    PUBLIC Microsoft::DirectX-Headers Microsoft::DirectX-Guids dxgi dxcompiler
    PRIVATE unofficial::d3d12-memory-allocator Microsoft::DirectXAgilitySDK Microsoft::WinPixEventRuntime
)

# Copy shaders.
ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/sample.hlsl" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying 'sample.hlsl' to runtime directory..."
)

ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/luma_0.nv12" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/luma_1.nv12" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/chroma_0.nv12" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/chroma_1.nv12" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying images to runtime directory..."
)

# Copy the DXC runtime to the project build dir.
IF(DXCOMPILER_DLL)
    ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DXCOMPILER_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying ${DXCOMPILER_DLL} to runtime directory..."
    )
ENDIF(DXCOMPILER_DLL)

IF(DXIL_DLL)
    ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DXIL_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying ${DXIL_DLL} to runtime directory..."
    )
ENDIF(DXIL_DLL)