#!/usr/bin/env bash
# 本地一键打包 APK（Linux / macOS / WSL / Git Bash）
#
# 前置条件：
#   1. JDK 17
#   2. Android SDK + 以下组件：
#        sdkmanager --install "platforms;android-34" "build-tools;34.0.0" "ndk;25.2.9519653" "cmake;3.22.1"
#   3. git
#
# 用法：
#   ./build_apk.sh              # debug
#   ./build_apk.sh release

set -euo pipefail
BUILD_TYPE="${1:-debug}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_DIR="$ROOT/android"

info() { printf '\033[0;36m[信息]\033[0m %s\n' "$*"; }
warn() { printf '\033[0;33m[警告]\033[0m %s\n' "$*"; }
fail() { printf '\033[0;31m[错误]\033[0m %s\n' "$*" >&2; exit 1; }

# ---- 1. Java ----
command -v java >/dev/null 2>&1 || fail "未找到 java，请安装 JDK 17"
JAVA_VER="$(java -version 2>&1 | head -n 1)"
case "$JAVA_VER" in
  *17.*) ;;
  *) warn "建议使用 JDK 17，当前：$JAVA_VER" ;;
esac

# ---- 2. Android SDK ----
SDK=""
for c in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Android/Sdk" "$HOME/Library/Android/sdk"; do
  [ -n "$c" ] && [ -d "$c" ] && { SDK="$c"; break; }
done
[ -n "$SDK" ] || fail "未找到 Android SDK，请设置 ANDROID_HOME"
export ANDROID_HOME="$SDK"
export ANDROID_SDK_ROOT="$SDK"
info "Android SDK: $SDK"
[ -d "$SDK/ndk/25.2.9519653" ] || fail "缺少 NDK 25.2.9519653，请执行 sdkmanager --install 'ndk;25.2.9519653'"

# ---- 3. SDL2 ----
if [ ! -f "$ROOT/thirdparty/SDL/CMakeLists.txt" ]; then
  info "拉取 SDL2 ..."
  command -v git >/dev/null 2>&1 || fail "未找到 git"
  mkdir -p "$ROOT/thirdparty"
  git clone --depth 1 -b release-2.30.x https://github.com/libsdl-org/SDL.git "$ROOT/thirdparty/SDL"
fi

# ---- 4. SDL Java 端 ----
SDL_JAVA_SRC="$ROOT/thirdparty/SDL/android-project/app/src/main/java/org/libsdl/app"
SDL_JAVA_DST="$ANDROID_DIR/app/src/main/java/org/libsdl/app"
[ -d "$SDL_JAVA_SRC" ] || fail "SDL 源码里找不到 $SDL_JAVA_SRC"
mkdir -p "$SDL_JAVA_DST"
cp "$SDL_JAVA_SRC"/*.java "$SDL_JAVA_DST/"

# ---- 5. stb（可选）----
if [ ! -f "$ROOT/thirdparty/stb/stb_image.h" ]; then
  warn "未找到 thirdparty/stb，APK 将无法解码 PNG/字体/OGG（可照常编译）"
fi

# ---- 6. Gradle ----
if [ ! -x "$ANDROID_DIR/gradlew" ]; then
  if command -v gradle >/dev/null 2>&1; then
    info "生成 Gradle Wrapper ..."
    (cd "$ANDROID_DIR" && gradle wrapper --gradle-version 8.2 --distribution-type bin)
  else
    fail "未找到 gradlew，也没有 gradle 命令。请安装 Gradle 8.2 或 Android Studio。"
  fi
fi
chmod +x "$ANDROID_DIR/gradlew"

# ---- 7. 构建 ----
TASK="assembleDebug"
[ "$BUILD_TYPE" = "release" ] && TASK="assembleRelease"
info "开始构建：$TASK"
(cd "$ANDROID_DIR" && ./gradlew "$TASK" --stacktrace)

APK="$(find "$ANDROID_DIR/app/build/outputs/apk/$BUILD_TYPE" -name '*.apk' -printf '%T@ %p\n' 2>/dev/null \
       | sort -rn | head -n 1 | cut -d' ' -f2-)"
[ -n "$APK" ] || fail "构建结束但没找到 APK"

printf '\n\033[0;32m========== 构建成功 ==========\033[0m\n'
printf '\033[0;32mAPK: %s\033[0m\n' "$APK"
printf '\033[0;32m大小: %s\033[0m\n' "$(du -h "$APK" | cut -f1)"
printf '\n安装到手机：\n  adb install -r "%s"\n' "$APK"
