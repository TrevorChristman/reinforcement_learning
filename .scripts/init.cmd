REM Integration points for toolchain customization
IF NOT DEFINED nugetPath (
    SET nugetPath=nuget
)

IF NOT DEFINED msbuildPath (
    CALL %~dp0find-vs2017.cmd
)

IF NOT DEFINED vstestPath (
    CALL %~dp0find-vs2017.cmd
)

IF NOT DEFINED msbuildPath (
    IF NOT EXIST "%VsInstallDir%\MSBuild\15.0\Bin\MSBuild.exe" (
        IF EXIST "%VsInstallDir%\MSBuild\Current\Bin\MSBuild.exe" (
            SET "msBuildPath=%VsInstallDir%\MSBuild\Current\Bin\MSBuild.exe"
        ) else (
            ECHO ERROR: MsBuild couldn't be found
            EXIT /b 1
        )
    ) ELSE (
        SET "msBuildPath=%VsInstallDir%\MSBuild\15.0\Bin\MSBuild.exe"
    )
)

IF NOT DEFINED vstestPath (
    IF NOT EXIST "%VsInstallDir%\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe" (
        ECHO ERROR: vstest.console couldn't be found
        EXIT /b 1
    ) ELSE (
        SET "vstestPath=%VsInstallDir%\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe"
    )
)

IF NOT DEFINED dotnetPath (
    SET dotnetPath=dotnet
)

IF NOT DEFINED VCPKG_INSTALLATION_ROOT (
    ECHO ERROR: VCPKG_INSTALLATION_ROOT is not configured. Cannot find vcpkg.
    EXIT /b 1
)

SET "vcpkgPath=%VCPKG_INSTALLATION_ROOT%\vcpkg.exe"
SET "VcpkgIntegration=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\msbuild\vcpkg.targets"
SET "VcpkgCmake=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake"

REM Repo-specific paths
IF NOT DEFINED rlRoot (
    SET rlRoot=%~dp0..
)

REM Repo-specific paths
IF NOT DEFINED vwBinaryParserRoot (
    SET vwBinaryParserRoot=%~dp0..\external_parser
)

IF NOT DEFINED SkipAzureFactories (
    SET "SkipAzureFactories=false"
)