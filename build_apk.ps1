# 本地一键打包 APK（Windows / PowerShell）
#
# 前置条件：
#   1. JDK 17（https://adoptium.net/）
#   2. Android SDK，且已安装：
#        platforms;android-34  build-tools;34.0.0  ndk;25.2.9519653  cmake;3.22.1
#      安装命令（cmdline-tools 就位后）：
#        sdkmanager --install "platforms;android-34" "build-tools;34.0.0" "ndk;25.2.9519653" "cmake;3.22.1"
#   3. git
#
# 用法：
#   .\build_apk.ps1                 # debug
#   .\build_apk.ps1 -BuildType release

param(
    [ValidateSet('debug', 'release')]
    [string]$BuildType = 'debug'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$AndroidDir = Join-Path $Root 'android'

function Fail([string]$msg) { Write-Host "[错误] $msg" -ForegroundColor Red; exit 1 }
function Info([string]$msg) { Write-Host "[信息] $msg" -ForegroundColor Cyan }

# ---- 1. Java ----
if (-not (Get-Command java -ErrorAction SilentlyContinue)) {
    Fail '未找到 java。请安装 JDK 17 并把 java 加入 PATH。'
}
$javaVer = (java -version 2>&1) -join ' '
if ($javaVer -notmatch '17\.') { Write-Host "[警告] 建议使用 JDK 17，当前：$($javaVer.Split("`n")[0])" -ForegroundColor Yellow }

# ---- 2. Android SDK ----
$sdk = $null
foreach ($candidate in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk'))) {
    if ($candidate -and (Test-Path $candidate)) { $sdk = $candidate; break }
}
if (-not $sdk) { Fail '未找到 Android SDK。请设置 ANDROID_HOME，或安装 Android Studio。' }
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
Info "Android SDK: $sdk"

$ndkPath = Join-Path $sdk 'ndk\25.2.9519653'
if (-not (Test-Path $ndkPath)) { Fail "缺少 NDK 25.2.9519653。请执行：sdkmanager --install `"ndk;25.2.9519653`"" }

# ---- 3. SDL2 ----
$sdlDir = Join-Path $Root 'thirdparty\SDL'
if (-not (Test-Path (Join-Path $sdlDir 'CMakeLists.txt'))) {
    Info '拉取 SDL2 ...'
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail '未找到 git' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sdlDir) | Out-Null
    & git clone --depth 1 -b release-2.30.x https://github.com/libsdl-org/SDL.git $sdlDir
    if ($LASTEXITCODE -ne 0) { Fail 'SDL2 拉取失败' }
}

# ---- 4. SDL Java 端 ----
$sdlJavaSrc = Join-Path $sdlDir 'android-project\app\src\main\java\org\libsdl\app'
$sdlJavaDst = Join-Path $AndroidDir 'app\src\main\java\org\libsdl\app'
if (-not (Test-Path $sdlJavaSrc)) { Fail "SDL 源码里找不到 $sdlJavaSrc" }
New-Item -ItemType Directory -Force -Path $sdlJavaDst | Out-Null
Copy-Item (Join-Path $sdlJavaSrc '*.java') $sdlJavaDst -Force

# ---- 5. stb（可选）----
$stbDir = Join-Path $Root 'thirdparty\stb'
if (-not (Test-Path (Join-Path $stbDir 'stb_image.h'))) {
    Write-Host '[警告] 未找到 thirdparty/stb，APK 将无法解码 PNG/字体/OGG（可照常编译）' -ForegroundColor Yellow
}

# ---- 6. Gradle ----
$gradlew = Join-Path $AndroidDir 'gradlew.bat'
if (-not (Test-Path $gradlew)) {
    if (Get-Command gradle -ErrorAction SilentlyContinue) {
        Info '生成 Gradle Wrapper ...'
        Push-Location $AndroidDir
        & gradle wrapper --gradle-version 8.2 --distribution-type bin
        Pop-Location
        if ($LASTEXITCODE -ne 0) { Fail '生成 gradle wrapper 失败' }
    } else {
        Fail '未找到 gradlew.bat，也没有 gradle 命令。请安装 Gradle 8.2 或 Android Studio。'
    }
}

# ---- 7. 构建 ----
$task = if ($BuildType -eq 'release') { 'assembleRelease' } else { 'assembleDebug' }
Info "开始构建：$task"
Push-Location $AndroidDir
& $gradlew $task --stacktrace
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { Fail "构建失败（exit=$code）。把完整报错发出来即可。" }

$apk = Get-ChildItem -Path (Join-Path $AndroidDir "app\build\outputs\apk\$BuildType") -Filter '*.apk' |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) { Fail '构建结束但没找到 APK' }

Write-Host ''
Write-Host '========== 构建成功 ==========' -ForegroundColor Green
Write-Host "APK: $($apk.FullName)" -ForegroundColor Green
Write-Host "大小: {0:N2} MB" -f ($apk.Length / 1MB) -ForegroundColor Green
Write-Host ''
Write-Host '安装到手机：'
Write-Host "  adb install -r `"$($apk.FullName)`""
