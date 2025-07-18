@ECHO OFF

setlocal

set ROOT_DIR=%CD%
set OUTPUT_ROOT_DIR=%CD%/../../lib/Android

REM NDK OVERRIDE. Use a different NDK then your environment variable NDKROOT by setting it here
REM set NDKROOT=H:\ndk\21.1.6352462

REM set PATH=E:\cmake-3.19.0-rc1-win64-x64\bin;%PATH%
set PATH=%ANDROID_HOME%/cmake/3.22.1/bin
set PATH_TO_THIRDPARTY=%ROOT_DIR%/../../../..
set PATH_TO_PNG=%PATH_TO_THIRDPARTY%/libpng/libPNG-1.6.44

set PATH_TO_CMAKE_FILE=%ROOT_DIR%/../../
set PATH_TO_CROSS_COMPILE_CMAKE_FILES=%NDKROOT%/build/cmake/android.toolchain.cmake
set ANDROID_BUILD_PATH_ROOT=%ROOT_DIR%/Build

set PATH_TO_CROSS_COMPILE_CMAKE_FILES=%NDKROOT%/build/cmake/android.toolchain.cmake
set ANDROID_NMAKE_EXEC=%NDKROOT%/prebuilt/windows-x86_64/bin/make.exe

REM Remove Build folder and recreate it to clear it out
if exist "%ANDROID_BUILD_PATH_ROOT%" (rmdir "%ANDROID_BUILD_PATH_ROOT%" /s/q)
mkdir "%ANDROID_BUILD_PATH_ROOT%"

REM Set any additional flags here
set CXXFLAGS="-std=c++14"
set AdditionalCompileArguments=-DFT_WITH_ZLIB=ON -DFT_WITH_PNG=ON -DFT_WITH_HARFBUZZ=OFF -DCMAKE_DISABLE_FIND_PACKAGE_HarfBuzz=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_BZip2=TRUE -DPNG_LIBRARY=%PATH_TO_PNG% -DPNG_PNG_INCLUDE_DIR=%PATH_TO_PNG% -DANDROID_STL=c++_static 

set OutputFileName=libfreetype
set OutputDebugFileAddition=d
set OutputFileExtension=.a

rem ----------------------------------------------------------------------------------
rem --                            ARM64(Debug)                                      --
rem ----------------------------------------------------------------------------------
echo Building FreeType makefile for ARM64(Debug)
set AndroidABI=arm64-v8a
set AndroidAPILevel=android-26
set BuildType=Debug
set DestFolder=%OUTPUT_ROOT_DIR%/ARM64/%BuildType%

if exist "%DestFolder%" (rmdir "%DestFolder%" /s/q)
mkdir "%DestFolder%"

set CurrentAndroidBuildDir=%ANDROID_BUILD_PATH_ROOT%\%AndroidABI%\%BuildType%
mkdir "%CurrentAndroidBuildDir%"
cd %CurrentAndroidBuildDir%

cmake -G"MinGW Makefiles" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_TOOLCHAIN_FILE="%PATH_TO_CROSS_COMPILE_CMAKE_FILES%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM="%ANDROID_NMAKE_EXEC%" -DANDROID_NATIVE_API_LEVEL="%AndroidAPILevel%" -DANDROID_ABI="%AndroidABI%" -DANDROID_STL="c++_shared" -DCMAKE_BUILD_TYPE=%BuildType% -DCMAKE_CXX_FLAGS=%CXXFLAGS% %AdditionalCompileArguments% %PATH_TO_CMAKE_FILE% 
cmake --build .

move /y "%OutputFileName%%OutputDebugFileAddition%%OutputFileExtension%" "%DestFolder%\%OutputFileName%%OutputDebugFileAddition%%OutputFileExtension%"


rem ----------------------------------------------------------------------------------
rem --                            ARM64(Release)                                    --
rem ----------------------------------------------------------------------------------
echo Building FreeType makefile for ARM64(Release)
set BuildType=Release
set DestFolder=%OUTPUT_ROOT_DIR%/ARM64/%BuildType%

if exist "%DestFolder%" (rmdir "%DestFolder%" /s/q)
mkdir "%DestFolder%"

set CurrentAndroidBuildDir=%ANDROID_BUILD_PATH_ROOT%\%AndroidABI%\%BuildType%
mkdir "%CurrentAndroidBuildDir%"
cd %CurrentAndroidBuildDir%

cmake -G"MinGW Makefiles" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_TOOLCHAIN_FILE="%PATH_TO_CROSS_COMPILE_CMAKE_FILES%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM="%ANDROID_NMAKE_EXEC%" -DANDROID_NATIVE_API_LEVEL="%AndroidAPILevel%" -DANDROID_ABI="%AndroidABI%" -DANDROID_STL="c++_shared" -DCMAKE_BUILD_TYPE=%BuildType% -DCMAKE_CXX_FLAGS=%CXXFLAGS% %AdditionalCompileArguments% %PATH_TO_CMAKE_FILE% 
cmake --build .

move /y "%OutputFileName%%OutputFileExtension%" "%DestFolder%\%OutputFileName%%OutputFileExtension%"

rem ----------------------------------------------------------------------------------
rem --                            x64(Debug)                                        --
rem ----------------------------------------------------------------------------------
echo Building FreeType makefile for x64(Debug)
set AndroidABI=x86_64
set AndroidAPILevel=android-26
set BuildType=Debug
set DestFolder=%OUTPUT_ROOT_DIR%/x64/%BuildType%

if exist "%DestFolder%" (rmdir "%DestFolder%" /s/q)
mkdir "%DestFolder%"

set CurrentAndroidBuildDir=%ANDROID_BUILD_PATH_ROOT%\%AndroidABI%\%BuildType%
mkdir "%CurrentAndroidBuildDir%"
cd %CurrentAndroidBuildDir%

cmake -G"MinGW Makefiles" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_TOOLCHAIN_FILE="%PATH_TO_CROSS_COMPILE_CMAKE_FILES%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM="%ANDROID_NMAKE_EXEC%" -DANDROID_NATIVE_API_LEVEL="%AndroidAPILevel%" -DANDROID_ABI="%AndroidABI%" -DANDROID_STL="c++_shared" -DCMAKE_BUILD_TYPE=%BuildType% -DCMAKE_CXX_FLAGS=%CXXFLAGS% %AdditionalCompileArguments% %PATH_TO_CMAKE_FILE% 
cmake --build .

move /y "%OutputFileName%%OutputDebugFileAddition%%OutputFileExtension%" "%DestFolder%\%OutputFileName%%OutputDebugFileAddition%%OutputFileExtension%"


rem ----------------------------------------------------------------------------------
rem --                            x64(Release)                                      --
rem ----------------------------------------------------------------------------------
echo Building FreeType makefile for x64(Release)
set BuildType=Release
set DestFolder=%OUTPUT_ROOT_DIR%/x64/%BuildType%

if exist "%DestFolder%" (rmdir "%DestFolder%" /s/q)
mkdir "%DestFolder%"

set CurrentAndroidBuildDir=%ANDROID_BUILD_PATH_ROOT%\%AndroidABI%\%BuildType%
mkdir "%CurrentAndroidBuildDir%"
cd %CurrentAndroidBuildDir%

cmake -G"MinGW Makefiles" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_TOOLCHAIN_FILE="%PATH_TO_CROSS_COMPILE_CMAKE_FILES%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM="%ANDROID_NMAKE_EXEC%" -DANDROID_NATIVE_API_LEVEL="%AndroidAPILevel%" -DANDROID_ABI="%AndroidABI%" -DANDROID_STL="c++_shared" -DCMAKE_BUILD_TYPE=%BuildType% -DCMAKE_CXX_FLAGS=%CXXFLAGS% %AdditionalCompileArguments% %PATH_TO_CMAKE_FILE% 
cmake --build .

move /y "%OutputFileName%%OutputFileExtension%" "%DestFolder%\%OutputFileName%%OutputFileExtension%"

