package com.siglus.port

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.libsdl.app.SDLActivity
import java.io.File
import java.io.FileOutputStream

/**
 * SDL 入口 Activity。
 *
 * 数据目录约定：
 *   <filesDir>/game                 —— 默认数据根目录
 *   <filesDir>/siglus_root.txt      —— 内容为该目录的绝对路径，优先级更高
 *
 * 通过 SAF 选择外置目录后，本类会把内容拷进 <filesDir>/game 并写入 root 文件，
 * 然后重启 Activity 让 native 层重新加载。
 */
class SiglusActivity : SDLActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        // 必须在 super.onCreate() 之前完成：SDL 会在其中启动 native 线程
        bootstrapFromApkAssets()
        super.onCreate(savedInstanceState)
        hideSystemUi()
        val root = dataRoot()
        if (!File(root, "game.ini").exists()) {
            Log.i(TAG, "数据目录 $root 下没有 game.ini，提示选择目录")
            Toast.makeText(this, "请选择游戏数据目录", Toast.LENGTH_LONG).show()
            openDirectoryPicker()
        }
    }

    /** 把打包进 APK 的 assets/ 释放到 filesDir/game，让 APK 开箱即用 */
    private fun bootstrapFromApkAssets() {
        val dest = File(filesDir, "game")
        if (File(dest, "game.ini").exists()) return
        try {
            dest.mkdirs()
            copyAssetRecursively("", dest)
            File(filesDir, "siglus_root.txt").writeText(dest.absolutePath)
            Log.i(TAG, "已从 APK 资源释放数据到 ${dest.absolutePath}")
        } catch (e: Exception) {
            Log.w(TAG, "释放 APK 资源失败: ${e.message}")
        }
    }

    private fun copyAssetRecursively(relPath: String, destDir: File) {
        val names = assets.list(relPath)
        if (names.isNullOrEmpty()) {
            val name = relPath.substringAfterLast('/')
            if (name.isEmpty()) return
            assets.open(relPath).use { input ->
                File(destDir, name).outputStream().use { output -> input.copyTo(output) }
            }
            return
        }
        val dir = if (relPath.isEmpty()) destDir else File(destDir, relPath.substringAfterLast('/'))
        dir.mkdirs()
        for (n in names) {
            copyAssetRecursively(if (relPath.isEmpty()) n else "$relPath/$n", dir)
        }
    }

    override fun getLibraries(): Array<String> {
        // 我们产出的动态库叫 libsiglus.so，没有 libmain.so
        return arrayOf("SDL2", "siglus")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    private fun hideSystemUi() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.hide(
                android.view.WindowInsets.Type.statusBars() or
                    android.view.WindowInsets.Type.navigationBars()
            )
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                )
        }
    }

    // ------------------------------------------------------------ 数据导入
    private fun dataRoot(): File {
        val rootFile = File(filesDir, "siglus_root.txt")
        if (rootFile.exists()) {
            val txt = rootFile.readText().trim()
            if (txt.isNotEmpty()) return File(txt)
        }
        return File(filesDir, "game")
    }

    private fun openDirectoryPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
            addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            )
        }
        startActivityForResult(intent, REQ_PICK_DIR)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_PICK_DIR || resultCode != Activity.RESULT_OK) return
        val treeUri = data?.data ?: return
        contentResolver.takePersistableUriPermission(
            treeUri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
        val dest = File(filesDir, "game")
        CoroutineScope(Dispatchers.IO).launch {
            val count = copyTree(DocumentFile.fromTreeUri(this@SiglusActivity, treeUri), dest)
            withContext(Dispatchers.Main) {
                File(filesDir, "siglus_root.txt").writeText(dest.absolutePath)
                Log.i(TAG, "导入完成：$count 个文件 -> ${dest.absolutePath}")
                if (count > 0) relaunch()
                else Toast.makeText(this@SiglusActivity, "目录中没有文件", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun copyTree(src: DocumentFile?, destDir: File): Int {
        if (src == null || !src.exists()) return 0
        var count = 0
        destDir.mkdirs()
        for (child in src.listFiles()) {
            if (child.isDirectory) {
                count += copyTree(child, File(destDir, child.name ?: "sub"))
            } else {
                val name = child.name ?: continue
                val out = File(destDir, name)
                try {
                    contentResolver.openInputStream(child.uri)?.use { input ->
                        FileOutputStream(out).use { output -> input.copyTo(output) }
                    }
                    count++
                } catch (e: Exception) {
                    Log.w(TAG, "拷贝失败 $name: ${e.message}")
                }
            }
        }
        return count
    }

    private fun relaunch() {
        val intent = Intent(this, SiglusActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        finish()
        startActivity(intent)
    }

    // ------------------------------------------------------------ 影片播放
    /** 由 C++ 通过 JNI 调用：com/siglus/port/SiglusActivity.playMovie(String) */
    fun playMovie(path: String) {
        Log.i(TAG, "playMovie: $path")
        runOnUiThread {
            val intent = Intent(this, MovieActivity::class.java).apply {
                putExtra(MovieActivity.EXTRA_PATH, path)
            }
            startActivityForResult(intent, REQ_PLAY_MOVIE)
        }
    }

    companion object {
        private const val TAG = "SiglusActivity"
        private const val REQ_PICK_DIR = 1001
        private const val REQ_PLAY_MOVIE = 1002

        /** C/C++ 侧实现：VideoPlayer_android.cpp */
        @JvmStatic
        external fun nativeMovieFinished()
    }
}
