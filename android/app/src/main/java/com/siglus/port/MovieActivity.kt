package com.siglus.port

import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.VideoView
import java.io.File

/**
 * 影片播放：Siglus 的 .omv 需要先在 PC 上剥离私有头转成标准 mp4，
 * 放到数据目录的 movie/ 下；运行时用系统解码器播放，避免自己写软解。
 *
 * 播放结束会回调 SiglusActivity.nativeMovieFinished()，让脚本继续。
 */
class MovieActivity : android.app.Activity() {

    private lateinit var video: VideoView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        video = VideoView(this)
        setContentView(video)

        val path = intent.getStringExtra(EXTRA_PATH) ?: run { finish(); return }
        val uri: Uri = if (path.startsWith("content://") || path.startsWith("file://")) {
            Uri.parse(path)
        } else {
            Uri.fromFile(File(path))
        }

        video.setVideoURI(uri)
        video.setOnPreparedListener { it.isLooping = false; video.start() }
        video.setOnErrorListener { _, what, extra ->
            Log.w(TAG, "播放失败 what=$what extra=$extra")
            notifyFinished(); true
        }
        video.setOnCompletionListener { notifyFinished() }
    }

    private fun notifyFinished() {
        try {
            SiglusActivity.nativeMovieFinished()
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "native 尚未就绪: ${e.message}")
        }
        finish()
    }

    companion object {
        const val EXTRA_PATH = "movie_path"
        private const val TAG = "MovieActivity"
    }
}
