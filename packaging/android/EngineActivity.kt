package com.rowlengine.game

import android.os.Bundle
import org.libsdl.app.SDLActivity

class EngineActivity : SDLActivity() {
    override fun getMainSharedObject(): String {
        return "librowl_engine.so"
    }

    override fun getLibraries(): Array<String> {
        return arrayOf(
            "rowl_engine"
        )
    }
}
