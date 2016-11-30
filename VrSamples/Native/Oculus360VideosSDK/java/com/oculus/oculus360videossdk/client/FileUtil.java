package com.oculus.oculus360videossdk.client;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.MediaStore;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.List;

/**
 * Created by 谢秋鹏 on 2016/5/27.
 */
public class FileUtil {
    public int contentLength;
    public static File file;

    // 读文件
    public static File readFromSDCard(String fileName) {
        File file = new File(Constants.PAPH_OCULUS, fileName);
        try {
            File path = new File(Constants.PAPH_OCULUS);
            if (!path.exists()) {
                path.mkdirs();
            }
            if (!file.exists()) {
                file.createNewFile();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        return file;
    }


    public static void downMp4(String inputMsg, File file) {
        try {

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /**
     * 转换 path路径
     */
    public static String getFilePathFromUri(Context c, Uri uri) {
        String filePath = null;
        if ("content".equals(uri.getScheme())) {
            String[] filePathColumn = {MediaStore.MediaColumns.DATA};
            ContentResolver contentResolver = c.getContentResolver();
            Cursor cursor = contentResolver.query(uri, filePathColumn, null, null, null);
            cursor.moveToFirst();
            int columnIndex = cursor.getColumnIndex(filePathColumn[0]);
            filePath = cursor.getString(columnIndex);
            cursor.close();
        } else if ("file".equals(uri.getScheme())) {
            filePath = new File(uri.getPath()).getAbsolutePath();
        }
        return filePath;
    }

    public static List<String> getFileList() {
        //new的一个File对象
        List<String> list = new ArrayList<String>();
        File f = new File(Constants.PAPH_OCULUS);
        if (f.isDirectory()) {
            for (File file : f.listFiles()) {
                if (file.getName().endsWith("mp4")) {
                    Log.e("www", file.getName());
                    list.add(file.getName());
                }
            }
        } else {
            Log.w("www", "not a firectory");
        }
        return list;
    }

    public static void delete(Uri uri) {
        File file = null;
        try {
            file = new File(new URI(uri.toString()));
        } catch (URISyntaxException e) {
            e.printStackTrace();
        }
        if (!file.exists()) {
            return;
        }
        if (file.isFile()) {
            file.delete();
            return;
        }

        if (file.isDirectory()) {
            File[] childFiles = file.listFiles();
            if (childFiles == null || childFiles.length == 0) {
                file.delete();
                return;
            }

            for (int i = 0; i < childFiles.length; i++) {
                delete(childFiles[i]);
            }
            file.delete();
        }
    }

    public static void delete(File file) {
        if (file.isFile()) {
            file.delete();
            return;
        }

        if (file.isDirectory()) {
            File[] childFiles = file.listFiles();
            if (childFiles == null || childFiles.length == 0) {
                file.delete();
                return;
            }

            for (int i = 0; i < childFiles.length; i++) {
                delete(childFiles[i]);
            }
            file.delete();
        }
    }
}