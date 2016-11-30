package com.oculus.oculus360videossdk.client;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.util.Log;

import java.io.File;

/**
 * Created by xieqi on 2016/8/11.
 */
public class Constants {
    //
    public static final String PAPH_DOWNLOAD = Environment.getExternalStorageDirectory() + "/" + Environment.DIRECTORY_DOWNLOADS + "/";
    public static final String PAPH_OCULUS = Environment.getExternalStorageDirectory() + "/" + "Oculus/360Videos/";
    public static final String PAPH_3D = Environment.getExternalStorageDirectory() + "/" + "Oculus/Movies/My Videos/3D/";

    public static String FILE_NAME = "test.mp4";
    public static String FILE_PATH = PAPH_OCULUS + FILE_NAME;

    // CONSTANTS
    public static final String DEFAULT_IP = "255.255.255.255";
    public static final int DEFAULT_PORT = 10001;
    public static final int DEFAULT_TIMEOUT = 5000;

    // 0 play 1 pause 2 Stop 3 replay
    public static int state = 2;

    private static File file = null;

    public static File getFile() {
        if (file == null) {
            file = new File(PAPH_OCULUS + FILE_NAME);
        }
        return file;
    }

    public static String getFileName(Context context) {
        SharedPreferences pre = PreferenceManager.getDefaultSharedPreferences(context);
        FILE_NAME = pre.getString("filename", "test.mp4");
        Log.d(Constants.class.getSimpleName(), "get " + FILE_NAME);
        return FILE_NAME;
    }

    public static void setFileName(String fileName, Context context) {
        FILE_NAME = fileName;
        SharedPreferences pre = PreferenceManager.getDefaultSharedPreferences(context);
        SharedPreferences.Editor editor = pre.edit();
        editor.putString("filename", fileName);
        editor.commit();
        Log.d(Constants.class.getSimpleName(), "set " + FILE_NAME);
    }

    public static void deleteFile() {
        file.delete();
        file = null;
    }
}
