package com.oculus.oculus360videossdk.client;

import android.util.Log;

import com.oculus.oculus360videossdk.App;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.Socket;
import java.net.UnknownHostException;

/**
 * Created by xieqi on 2016/9/6.
 */
public class Client02 {
    private String ip = "";
    private int port = 10002;
    private Socket socket = null;
    private DataInputStream reader = null;
    private Writer writer = null;
    //
    private int len = 0;
    private int videoIndex = 0;
    private byte[] bytes = new byte[1024 * 1024 * 5];
    //
    private int media = 0;
    private String name = "";
    private int size = 0;
    //
    private int fileCount = 0;

    //
    public String getClientNum() {
        String clientNumber = "";
        try {
            socket = new Socket(ip, port);
//            socket.setReceiveBufferSize();
//            socket.setSoTimeout(5000);-
            String mac = NetUtil.getMac();
            //构建IO
            InputStream is = socket.getInputStream();
            OutputStream os = socket.getOutputStream();
            //向服务器端发送一条消息
            writer = new OutputStreamWriter(os);
            writer.write(mac);
            writer.flush();
            Log.e(Client02.class.getSimpleName(), mac);
            // 读取服务器返回的消息
            reader = new DataInputStream(is);
            len = reader.read(bytes);
            clientNumber = new String(bytes, 0, len);
            Log.w(Client02.class.getSimpleName(), clientNumber);
        } catch (UnknownHostException e) {
            e.printStackTrace();
        } catch (IOException e) {
            e.printStackTrace();
        }
        return clientNumber;
    }

    public void recieveFile() {
        try {
            recieveFile(0);
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            try {
                if (media == fileCount - 1) {
                    Log.e(Client02.class.getSimpleName(), "socket close");
                    String back = "VR_READY;";
                    writer.write(back);
                    writer.flush();
                    socket.close();
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private void recieveFile(int unused) throws IOException {
        Log.e(Client02.class.getSimpleName(), len + " " + videoIndex);
        File download = FileUtil.readFromSDCard(name);
        FileOutputStream outFile = null;
        try {
            outFile = new FileOutputStream(download);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        }
        //
        int fileLen = len - (videoIndex + 1);
        Log.e(Client02.class.getSimpleName(), fileLen + " " + videoIndex);
        if (fileLen > 0) {
            outFile.write(bytes, videoIndex + 1, fileLen);
            outFile.flush();
        }
        //
        while ((len = reader.read(bytes)) != -1) {
            fileLen += len;
            if (fileLen >= size) {
                outFile.write(bytes, 0, len - (fileLen - size));
                outFile.flush();
                break;
            } else {
                outFile.write(bytes, 0, len);
                outFile.flush();
            }
        }
        //
        int length = "VR_MEDIA_OVER;".length();
        StringBuilder stringBuilder2 = new StringBuilder();
        for (int i = 0; i < length; i++) {
            String str = new String(bytes, len - (fileLen - size) + i, 1);
            stringBuilder2.append(str);
        }
        videoIndex = len - (fileLen - size) + length;
//        if (media == fileCount) {
//            length = "VR_FILE_OVER;".length();
//        }
        Log.w(Client02.class.getSimpleName(), stringBuilder2.toString());
        //
        String back = "VR_RECEIVE_MEDIA_SIZE_" + size + ";";
        writer.write(back);
        writer.flush();
        //
        outFile.close();
    }

    public int getFileInfo() {
        String readBuffer = "";
        try {
            readBuffer = getInfo();
        } catch (IOException e) {
            e.printStackTrace();
        }
        String[] info = readBuffer.split(";");
        String sent = info[0].split("_")[2];
        fileCount = Integer.valueOf(info[1].split("_")[2]);
        String tag = "sent " + sent + "\n"
                + "file " + fileCount;
        Log.w(Client02.class.getSimpleName(), tag);
        return fileCount;
    }

    public void getMediaInfo() {
        try {
            getMediaInfo(0);
        } catch (IOException e) {

        }
    }

    public String getMediaInfo(int index) throws IOException {
        Log.v(Client02.class.getSimpleName(), "getMediaInfo1 " + len + " " + videoIndex);
        StringBuilder stringBuilder = new StringBuilder();
        int sperate = 0;
        int buflen = len - (videoIndex + 1);
        if (buflen > 0) {
            for (int i = videoIndex + 1; i < len; i++) {
                videoIndex = i;
                if (bytes[i] == ';') {
                    sperate++;
                }
                if (sperate < 3) {
                    stringBuilder.append(new String(bytes, videoIndex, 1));
                } else if (sperate == 3) {
                    break;
                }
            }
        }
        while (sperate < 3 && (len = reader.read(bytes)) != -1) {
            Log.e(Client02.class.getSimpleName(), "process");
            for (int i = 0; i < len; i++) {
                videoIndex = i;
                if (bytes[videoIndex] == ';') {
                    sperate++;
                }
                if (sperate < 3) {
                    stringBuilder.append(new String(bytes, videoIndex, 1));
                } else if (sperate == 3) {
                    break;
                }
            }
            // buffer 太长
            if (sperate == 3) {
                break;
            }
        }
        Log.i(Client02.class.getSimpleName(), "getMediaInfo2 " + len + " " + videoIndex);
        String[] info = stringBuilder.toString().split(";");
        Log.d(Client02.class.getSimpleName(), stringBuilder.toString());
        media = Integer.valueOf(info[0].split("_")[2]);
        int length = "VR_MEDIANAME_".length();
        name = info[1].substring(length, info[1].length());
        size = Integer.valueOf(info[2].split("_")[2]);
        String tag = "media " + media + "\n "
                + "name " + name + "\n "
                + "size " + size;
        Log.w(Client02.class.getSimpleName(), tag);
        Constants.setFileName(name, App.getContext());
        return name;
    }

    private String getFileInfo(int len, int videoIndex, byte[] bytes) throws IOException {
        int sperate = 0;
        StringBuilder stringBuilder = new StringBuilder();
        while ((len = reader.read(bytes)) != -1) {
            for (int i = 0; i < len; i++) {
                videoIndex = i;
                if (bytes[videoIndex] == ';') {
                    sperate++;
                }
                if (sperate < 5) {
                    stringBuilder.append(new String(bytes, videoIndex, 1));
                } else if (sperate == 5) {
                    break;
                }
            }
            // buffer 太长
            if (sperate == 5) {
                break;
            }
        }
        return stringBuilder.toString();
    }

    private String getInfo() throws IOException {
        int sperate = 0;
        StringBuilder stringBuilder = new StringBuilder();
        while ((len = reader.read(bytes)) != -1) {
            for (int i = 0; i < len; i++) {
                videoIndex = i;
                if (bytes[videoIndex] == ';') {
                    sperate++;
                }
                if (sperate < 2) {
                    stringBuilder.append(new String(bytes, videoIndex, 1));
                } else if (sperate == 2) {
                    break;
                }
            }
            // buffer 太长
            if (sperate == 2) {
                break;
            }
        }
        Log.e(Client02.class.getSimpleName(), "getInfo " + len + " " + videoIndex);
        return stringBuilder.toString();
    }

    public void setIp(String ip) {
        this.ip = ip;
    }

}
