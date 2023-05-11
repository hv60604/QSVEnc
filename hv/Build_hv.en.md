
# hv fork for Simplified QSVEnc Windows build

- [Windows](./Build_hv.en.md#windows)
- [Linux - click here for rigaya instructions](https://github.com/rigaya/QSVEnc/blob/master/Install.en.md)

# Windows

### 0. Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/community/)
  - .NET
    - .NET Framewwork 4.8 SDK
    - .NET Framewwork 4.8.1 Targeting Pack
  - Code Tools
    - NuGet Package Manager
    - Text Template Transformation
  - Compilers, Build Tools, and Runtimes
    - C++ 2022 Redistributable Update
    - C++ CMake Tools for Windows
    - **C++ CLI Support for v143 build tools (Latest)**
    - MSBuild
    - MSVC v143 - VS C++ 2022 x64/x86 build tools (latest)
  - Debugging and Testing
    - C++ AddressSanitizer
    - C++ profiling tools
    - Just-In-Time debugger
  - Development Activities
    - C++ core features
    - InteliCode
    - JavaScript and TypeScript language support 
    - LiveShare
    - Python language support
    - Security issue analysis
  - Games and Graphics
    - Graphics debugger and GPU profiler for DirectX
  - SDKs, libraries, and frameworks
    - C++ ALT for latest v143 build tools (x86 & x64)
    - TypeScript Server
    - Windows11 SDK (10.0.22621.0)

Note that **C++ CLI Support for v143 build tools (Latest)** needs to be explicitly selected during Visual Studio install

To build QSVEnc, no additional components are required.

With this fork, the following are already included in the project hv folder and further updates can be placed there: 

-  [AviSynth+](https://github.com/AviSynth/AviSynthPlus)
-  [VapourSynth](http://www.vapoursynth.com/)
-  [Caption2Ass](https://github.com/maki-rxrz/Caption2Ass_PCR)
-  [OpenCL](https://github.com/KhronosGroup/OpenCL-Headers.git)  
  
building requires no extra environment variables for the items above.
Also, this fork of build 7.37 also supplies the following in it's original 1st-level folder location:

- [ffmpeg5_dlls](https://github.com/rigaya/ffmpeg5_dlls_for_hwenc.git)

...

### 1. Download source code

```Batchfile
git clone https://github.com/hv60604/QSVEnc --recursive

```

### 2. Build QSVEncC.exe / QSVEnc.auo

Open QSVEnc.sln and start build of QSVEnc by Visual Studio.

|  |For Debug build|For Release build|
|:--------------|:--------------|:--------|
|QSVEnc.auo (win32 only) | Debug | Release |060
|QSVEncC(64).exe | DebugStatic | RelStatic |

