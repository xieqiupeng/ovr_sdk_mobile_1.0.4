package com.oculus.oculus360videossdk.client;

import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.UnknownHostException;

/**
 * Created by xieqi on 2016/9/5.
 */
public class Client01 {

    private int port;
    private String host;
    byte[] data = new byte[256];

    public Client01(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public String listen() {
        String message = "";
        DatagramSocket socket = null;
        try {
            socket = new DatagramSocket(port);
            DatagramPacket packet = new DatagramPacket(data, data.length);
            //receive()是阻塞方法，会等待客户端发送过来的信息
            socket.receive(packet);
            message = new String(packet.getData(), 0, packet.getLength());
            Log.d(Client01.class.getSimpleName(), host + " " + port + " " + message);
        } catch (UnknownHostException e) {
            e.printStackTrace();
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            socket.disconnect();
            socket.close();
            Log.d(Client01.class.getSimpleName(), getIP(message));
            return getIP(message);
        }
    }

    public String getIP(String message) {
        String[] strs = message.split("_");
        return strs[strs.length - 1];
    }
}
