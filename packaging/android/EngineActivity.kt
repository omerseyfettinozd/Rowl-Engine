package com.rowlengine.game

import android.os.Bundle
import org.libsdl.app.SDLActivity

class EngineActivity : SDLActivity() {
    override fun getMainSharedObject(): String {
        return "libRowlEngineCore.so"
    }

    override fun getLibraries(): Array<String> {
        return arrayOf(
            "RowlEngineCore"
        )
    }
}
