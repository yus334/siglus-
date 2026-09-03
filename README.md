# SiglusPort —— SiglusEngine 跨平台兼容实现（路线二：原生重写）

用 C++17 + SDL2 重写的 Siglus 类引擎运行框架，同一份代码可编译到 **Windows / Linux / macOS / Android**。
目标是把 Siglus 系的视觉小说搬到安卓上原生运行，而不是靠 Wine 兼容层。

> 说明：本项目只提供**运行框架**。真实游戏的二进制格式（`.pkg` 索引、`.g00` 图像、`.nwa` 音频、
> `.ss` 脚本 opcode）没有公开文档，需要你用自带的 `siglus_probe` 工具拿**自己合法拥有的样本**校准
> 后填入。代码里这些位置都留成了明确的插件槽，不猜测、不硬编码。

---

## 1. 现在就能跑的部分

| 模块 | 状态 | 位置 |
|---|---|---|
| 主循环 / 状态机 | 可用 | `src/app/Engine.cpp` |
| 分层合成渲染（normal/add/mod/mul 混合） | 可用 | `src/gfx/Renderer.cpp` |
| 转场（交叉淡化 / 黑场 / 遮罩混合） | 可用 | `Renderer::DrawTransition` |
| 文本机（打字机、ruby 注音 `[漢字](かんじ)`、横排/纵排） | 可用（需 stb_truetype） | `src/text/Text.cpp` |
| 音频（BGM/SE/语音分轨、循环点、淡入淡出） | 可用 | `src/audio/Audio.cpp` |
| 触控手势（点击/长按快进/双击自动/双指菜单/上下滑） | 可用 | `src/input/Input.cpp` |
| 存档读档（脚本状态 + 图层快照） | 可用 | `Engine::SaveSlot / LoadSlot` |
| demo 文本方言脚本（自测） | 可用 | `assets/script/demo.asm` |
| 安卓工程（SDL Activity、SAF 导入数据、影片播放） | 可用 | `android/` |
| `.pkg` 索引解析 | **框架 + 校准工具** | `src/archive/Archive.cpp` |
| `.g00` 图像解码 | **待实现槽位** | `G00Decoder` |
| `.nwa` 音频解码 | **待实现槽位** | `NwaDecoder` |
| `.omv` 影片 | **离线转码成 mp4 后播放** | `MovieActivity.kt` |
| `.ss` 字节码执行 | **表驱动，需填 opcode 表** | `OpcodeTable` / `Disassemble` |

---

## 2. 目录结构

```
siglus_android/
├── CMakeLists.txt            顶层构建（PC 可执行 + Android 动态库）
├── cmake/Deps.cmake          SDL2 / stb 依赖获取
├── assets/                   默认数据根（game.ini 就在这里）
│   ├── game.ini              分辨率、脚本、字体、文本框、BGM 循环点
│   ├── script/demo.asm       demo 方言自检脚本
│   ├── opcodes/ss_opcodes.csv  .ss 指令表模板（待校准）
│   └── pkg_layout.ini        .pkg 布局模板（待校准）
├── src/
│   ├── core/                 日志 / 流 / 文件系统 / INI / UTF-8 / hexdump
│   ├── archive/              IArchive：DirArchive（目录）、PkgArchive（.pkg）
│   ├── media/                图像/音频/视频解码器注册表
│   ├── script/               opcode 表、反汇编器、VM 解释器
│   ├── gfx/                  SDL_Renderer 2D 后端（分层、混合、转场）
│   ├── text/                 字体图集 + 排版（横排/纵排/ruby）
│   ├── audio/                混音器（BGM/SE/Voice + 循环点）
│   ├── input/                手势识别
│   ├── app/                  Engine 主循环 + 入口
│   └── platform/             VideoPlayer（安卓走 JNI + 系统播放器）
├── tools/probe.cpp           siglus_probe：探测 / 反汇编 / 解包
└── android/                  Gradle 工程（Kotlin + CMake）
```

---

## 3. 依赖

**必需**
- CMake ≥ 3.20、支持 C++17 的编译器
- SDL2 ≥ 2.0.10（用到 `SDL_RenderCopyF` 与自定义混合模式）

**可选但强烈建议**
- [stb](https://github.com/nothings/stb) 单头文件：提供 PNG/JPG 解码、TTF 字形栅格化、OGG 解码

```bash
# 方式 A：放进工程内 thirdparty/stb（自动识别）
git clone --depth 1 https://github.com/nothings/stb thirdparty/stb

# 方式 B：任意目录，configure 时指定
cmake -DSIGLUS_STB_INCLUDE_DIR=/path/to/stb ...
```

没有 stb 时引擎仍能编译运行，但图片/文字/ogg 都无法解码（会打印明确的告警）。

---

### 字体

`assets/font/default.ttf` 已附带一份黑体（9.3 MB，系统自带字体），
覆盖简体中文以及 GBK 里的日文假名，开箱即可显示文字，ruby 注音也能正常渲染。

想换成别的字体，直接覆盖该文件即可；也可以改 `game.ini` 指向别处：

```ini
[text]
font       = font/YuGothM.ttc
face_index = 1          ; .ttc 是字体集合，指定用其中第几个，默认 0
```

`.ttc` / `.ttf` / `.otf` 都支持——代码按文件头的 `ttcf` 标记判断，不依赖扩展名。

## 4. PC 端构建与运行

```bash
# 有本地 SDL2
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# 没有本地 SDL2（自动 FetchContent 下载）
cmake -S . -B build -DSIGLUS_FETCH_DEPS=ON
cmake --build build -j

# 用仓库自带的 assets 直接跑 demo
./build/siglus_pc --data assets
```

命令行参数：

```
--data <dir>     数据根目录（默认 assets）
--ini  <file>    配置文件（默认 game.ini）
--fullscreen     全屏
--portrait       竖屏
```

操作：鼠标左键/空格推进，长按或 `S` 快进，`A` 自动，`H` 历史，`D` 隐藏 UI，`M` 菜单，
`F5` 快速存档，`F9` 快速读档。

---

## 5. 打包成 APK

三种方式任选。**本机没装工具链的话直接走 5.1**，push 到 GitHub 就能在 Actions 里下载 APK。

### 5.1 云端构建（GitHub Actions，零安装）

工程里已经带了 `.github/workflows/build-apk.yml`：

```bash
git init && git add . && git commit -m "init"
git remote add origin <你的仓库地址>
git push -u origin main
```

推送后 Actions 会自动安装 JDK / Android SDK / NDK / SDL2 / stb 并编译，
产物在 Actions 详情页的 **Artifacts → siglus-apk** 里下载（也可手动 `Run workflow` 触发）。
打 `v*` 标签时还会自动把 APK 挂到 GitHub Release。

想出正式签名包：在仓库 Settings → Secrets 里配置
`SIGLUS_KEYSTORE`（文件）、`SIGLUS_KEYSTORE_PASSWORD`、`SIGLUS_KEY_ALIAS`、`SIGLUS_KEY_PASSWORD`，
然后把 workflow 的 `build_type` 选 `release`。不配置也能编译，只是用 debug 证书签名。

### 5.2 本地一键脚本

先装好 JDK 17 与 Android SDK，并安装 SDK 组件：

```bash
sdkmanager --install "platforms;android-34" "build-tools;34.0.0" "ndk;25.2.9519653" "cmake;3.22.1"
```

然后：

```powershell
# Windows
.\build_apk.ps1                  # debug
.\build_apk.ps1 -BuildType release
```

```bash
# Linux / macOS / WSL
./build_apk.sh
./build_apk.sh release
```

脚本会自动拉取 SDL2（`thirdparty/SDL`）、拷贝 SDL 的 Java 端、生成 Gradle Wrapper 并构建，
最后打印 APK 路径与 `adb install` 命令。

### 5.3 手工步骤（Android Studio 用户）

#### 5.3.1 准备 SDL2

```bash
git clone --depth 1 -b release-2.30.x https://github.com/libsdl-org/SDL.git thirdparty/SDL
```

把 SDL 的 Java 端拷进工程：

```bash
mkdir -p android/app/src/main/java/org/libsdl/app
cp thirdparty/SDL/android-project/app/src/main/java/org/libsdl/app/*.java \
   android/app/src/main/java/org/libsdl/app/
```

#### 5.3.2 编译

用 Android Studio 打开 `android/` 目录直接 Run 即可。
命令行方式：在 `gradle.properties` 里设置（或用环境变量 `SIGLUS_SDL2_ROOT`）：

```
SDL2_DIR=../thirdparty/SDL
```

```bash
cd android
./gradlew assembleDebug
# 产物：app/build/outputs/apk/debug/app-debug.apk
```

#### 5.3.3 放数据

默认数据根目录是 `<filesDir>/game`。构建时 `assets/` 会被一起打进 APK，
首次启动自动释放到该目录（**装完即可运行 demo**）。替换成自己的游戏数据有以下几种方式：

1. 首次启动会自动弹出 SAF 目录选择器，选外置目录后拷贝到内部存储；
2. `adb push` 到 `/sdcard/Android/data/com.siglus.port/files/game/`；
3. 把资源打进 APK 的 `assets/`，改 Kotlin 在启动时释放到 `filesDir/game`。

也可以在 `<filesDir>/siglus_root.txt` 里写一行绝对路径，优先级最高。

#### 5.3.4 影片

`.omv` 是私有头封装的 MPEG 流，运行时不做软解。请在 PC 上剥离头部转成标准 `mp4`，
放到数据根的 `movie/<名称>.mp4`，脚本里 `movie <名称>` 即可。

---

## 6. 接入真实游戏的完整流程

### 第 1 步：探测封包

```bash
./build/siglus_probe Scene.pkg --dump 256                 # 看头部
./build/siglus_probe Scene.pkg --pkg                      # 列出候选布局
# 把候选填进 assets/pkg_layout.ini 后验证：
./build/siglus_probe Scene.pkg --pkg-layout pkg_layout.ini
# 文件名看起来正常了就解包：
./build/siglus_probe Scene.pkg --pkg-layout pkg_layout.ini --pkg-extract out
```

### 第 2 步：补齐解码器

- **`.g00`**：`src/media/Decoder.cpp` 的 `G00Decoder`。先用 `siglus_probe xxx.g00 --dump 64`
  看版本标记，再按版本实现解压（Visual Art's 的 g00 有多个变体）。
- **`.nwa`**：`NwaDecoder`。NWA 是有损压缩，需要实现 DPCM/ADPCM 还原。
- 过渡期可以先用 `wav`/`png`/`ogg` 占位跑通流程，再逐个替换。

### 第 3 步：`.ss` 指令表

```bash
./build/siglus_probe out/scene/xxx.ss --ss opcodes/ss_opcodes.csv --ss-out dump.txt
```

对照 dump 修正 `ss_opcodes.csv` 的操作码与操作数宽度，直到"无法识别"的指令数降到很低。
指令名要与 `Script.cpp::RegisterBuiltinHandlers` 里的名字对上（大小写不敏感），
否则用 `vm_.RegisterHandler("名字", 回调)` 自行注册。

### 第 4 步：填 BGM 循环点

循环信息一般藏在游戏数据里。提取后写到 `game.ini`：

```ini
[loop]
bgm01_start  = 123456
bgm01_length = 2345678
```

### 第 5 步：切换方言

```ini
[game]
dialect = ss
script  = scene/main.ss
opcodes = opcodes/ss_opcodes.csv
archive = Scene.pkg
archive_type = pkg
```

---

## 7. 架构与扩展点

```
Engine ── IScriptHost ──► VM (Script) ──► 表现层 (gfx / text / audio)
   │                          ▲
   │                     OpcodeTable（CSV 表驱动，换游戏不改代码）
   └── IArchive ──► DirArchive / PkgArchive ──► ImageRegistry / AudioRegistry
```

- **换渲染后端**：实现一个新的 `Renderer`（GLES3/Vulkan 都行），`Engine` 只依赖 `gfx/Renderer.h`
  的接口。需要减法混合或自定义 shader 时再上 GL 后端。
- **换解码器**：`ImageRegistry::Register` / `AudioRegistry::Register` 注册自己的实现即可。
- **加指令**：`vm_.RegisterHandler("名字", [](VM&, const Insn&) { ... })`。
- **换字体方案**：`text/Font` 目前用 stb_truetype；需要复杂排版（阿拉伯文、竖排标点旋转）
  时替换成 FreeType + HarfBuzz，接口不变。

---

## 8. 已知限制

- 渲染后端 `SDL_Renderer` 不支持减法混合；有需求请加 GL 后端。
- 遮罩转场的"阈值推进"是用遮罩 alpha 整体缩放近似的，不是逐像素阈值；需要精确效果请走 GL 后端。
- BGM 目前整段解码进内存，长曲子需要改成流式解码（`IAudioDecoder` 接口已经预留）。
- 音频不做重采样，解码器采样率必须等于输出设备采样率。
- `.ss` 反汇编按"定长指令"扫描；若目标脚本是变长栈式字节码，`Disassemble` 需要改成栈式解释器。
- 安卓端选完目录需要重启 Activity 才生效。

---

## 9. 路线图

1. `G00Decoder` / `NwaDecoder` 实装（依赖样本）
2. BGM 流式解码 + 采样率转换
3. GLES3 渲染后端（减法混合、逐像素遮罩转场、着色器特效）
4. 立绘差分合成、口型/眨眼动画、CG 模式、回想模式
5. 云存档与多槽位存档 UI
6. 文本钩子（日语 → 中文替换表 / AI 翻译注入）
