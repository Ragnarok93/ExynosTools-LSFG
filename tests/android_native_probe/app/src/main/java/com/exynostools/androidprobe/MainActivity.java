package com.exynostools.androidprobe;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("phase3c_probe");
    }

    private static native String runProbe(String nativeLibraryDir, String filesDir);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView view = new TextView(this);
        view.setTextSize(14.0f);
        view.setText(runProbe(getApplicationInfo().nativeLibraryDir, getFilesDir().getAbsolutePath()));
        setContentView(view);
    }
}
