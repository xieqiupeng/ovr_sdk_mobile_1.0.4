package com.oculus.oculus360videossdk.client;

import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.UnknownHostException;

/**
 * Created by xieqi on 2016/9/13.
 */
public class Client01Command {
    private int port;
    private String host;
    private String lastMessage = "";
    byte[] data = new byte[256];
    String[] command = new String[]{"COMMAND_PLAY", "COMMAND_PAUSE", "COMMAND_STOP", "COMMAND_REPLAY", "COMMAND_SEEK_"};

    public Client01Command(String host, int port) {
        this.host = host;
        this.port = port;
    }

    // Interface
    public interface Listener {
        public void setMessage(String sp1);
    }

    private Listener listener = null;

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public String listen() {
        String message = "";
        DatagramSocket socket = null;
        try {
            socket = new DatagramSocket(port);
            DatagramPacket packet = new DatagramPacket(data, data.length);
            while (true) {
                Thread.currentThread().join(500);
                socket.receive(packet);
                message = new String(packet.getData(), 0, packet.getLength());
                Log.d(Client01Command.class.getSimpleName(), host + " " + port + " " + message);
                for (String str : command) {
                    if (str.equals(message)) {
                        break;
                    }
                }
                if (!message.equals(lastMessage)) {
                    lastMessage = message;
                    listener.setMessage(message);
                }
            }
        } catch (UnknownHostException e) {
            e.printStackTrace();
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            socket.disconnect();
            socket.close();
            Log.d(Client01Command.class.getSimpleName(), message);
            return message;
        }
    }

}
