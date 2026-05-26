"""PlatformIO extra script: link libaquestalk.a (CoreS3 / esp32s3 用) を強制リンクする。

build_flags の -l:libaquestalk.a 形式だと SCons / PlatformIO が `.a` 拡張子を
剥がしてしまい `-l:libaquestalk` になってリンクに失敗するため、SCons の LIBS に
File オブジェクトで直接渡す。AquesTalk ESP32 Ver.2.4.x の libaquestalk.a を
firmware/lib/AquesTalkTTS/src/ に配置した前提。
"""
import os

Import("env")  # noqa: F821 (provided by SCons)

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
AQ_LIB = os.path.join(PROJECT_DIR, "firmware", "lib", "AquesTalkTTS", "src", "libaquestalk.a")

if not os.path.isfile(AQ_LIB):
    print(f"[link_aquestalk] WARN: {AQ_LIB} not found. See firmware/lib/AquesTalkTTS/README.md")
else:
    env.Append(LIBS=[File(AQ_LIB)])  # noqa: F821
    print(f"[link_aquestalk] linked: {AQ_LIB}")
