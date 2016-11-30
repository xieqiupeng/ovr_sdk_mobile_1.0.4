package com.oculus.oculus360videossdk;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;

import com.oculus.oculus360videossdk.client.Client01;
import com.oculus.oculus360videossdk.client.Client01Command;
import com.oculus.oculus360videossdk.client.Client02;
import com.oculus.oculus360videossdk.client.Client03;
import com.oculus.oculus360videossdk.client.Constants;

import java.lang.ref.WeakReference;
import java.util.logging.Logger;

import rx.Observable;
import rx.android.schedulers.AndroidSchedulers;
import rx.functions.Action1;
import rx.functions.Func1;
import rx.schedulers.Schedulers;

/**
 * Created by xieqi on 2016/9/19.
 */
public class App extends Application {
    private static App instance;
    private String ip = "";
    private WeakReference<Activity> mCurrentActivity;

    @Override
    public void onCreate() {
        super.onCreate();
        //
        instance = this;
        getIp();
        Constants.getFileName(getApplicationContext());
        //
        registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks() {
            @Override
            public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
                mCurrentActivity = new WeakReference<Activity>(activity);
            }

            @Override
            public void onActivityStarted(Activity activity) {
            }

            @Override
            public void onActivityResumed(Activity activity) {
                if (mCurrentActivity != null) {
                    mCurrentActivity.clear();
                    mCurrentActivity = null;
                }
                mCurrentActivity = new WeakReference<Activity>(activity);
            }

            @Override
            public void onActivityPaused(Activity activity) {
            }

            @Override
            public void onActivityStopped(Activity activity) {
            }

            @Override
            public void onActivitySaveInstanceState(Activity activity, Bundle outState) {

            }

            @Override
            public void onActivityDestroyed(Activity activity) {
//                mCurrentActivity.clear();
            }
        });
    }

    // getIp
    public void getIp() {
        Observable.just(ip)
                .filter(new Func1<String, Boolean>() {
                    @Override
                    public Boolean call(String ip) {
                        return TextUtils.isEmpty(ip);
                    }
                })
                .observeOn(Schedulers.io())
                .map(new Func1<String, String>() {
                    @Override
                    public String call(String s) {
                        Client01 client = new Client01(Constants.DEFAULT_IP, Constants.DEFAULT_PORT);
                        String ip = client.listen();
                        return ip;
                    }
                })
                .subscribeOn(AndroidSchedulers.mainThread())
                .subscribe(new Action1<String>() {
                    @Override
                    public void call(String ip) {
                        recieveFile(ip);
                        getCommand(ip);
                    }
                });
    }

    // command
    public void getCommand(String ip) {
        Observable.just(ip)
                .filter(new Func1<String, Boolean>() {
                    @Override
                    public Boolean call(String ip) {
                        return !TextUtils.isEmpty(ip);
                    }
                })
                .observeOn(Schedulers.io())
                .map(new Func1<String, String>() {
                    @Override
                    public String call(String ip) {
                        Client01Command client = new Client01Command(Constants.DEFAULT_IP, Constants.DEFAULT_PORT);
                        client.setListener(new Client01Command.Listener() {
                            @Override
                            public void setMessage(String str) {
                                MainActivity activity = (MainActivity) getCurrentShowActivity();
                                switch (str) {
                                    case "COMMAND_PAUSE":
                                        activity.pauseMovie();
                                        Constants.state = 1;
                                        break;
                                    case "COMMAND_STOP":
                                        Constants.state = 2;
                                        activity.stopMovie();
                                        activity.stop();
                                        break;
                                    case "COMMAND_REPLAY":
                                        Constants.state = 3;
                                        activity.stopMovie();
                                        activity.start(Constants.PAPH_OCULUS + Constants.FILE_NAME);
                                        activity.startMovieFromNative(Constants.PAPH_OCULUS + Constants.FILE_NAME);
                                        break;
                                    case "VR_3D":
                                        activity.set3D(true);
                                        break;
                                    case "VR_N3D":
                                        activity.set3D(false);
                                        break;
                                }
                                String seek = "COMMAND_SEEK_";
                                if (str.startsWith(seek)) {
                                    int miliseconds = Integer.valueOf(str.substring(seek.length(), str.length()));
                                    activity.seekToFromNative(miliseconds);
                                    Constants.state = miliseconds;
                                } else if (str.startsWith("COMMAND_PLAY_")) {
                                    String string = "COMMAND_PLAY_";
                                    String name = str.substring(string.length(), str.length());
                                    String path = Constants.PAPH_OCULUS + name;
                                    Log.w(Client01Command.class.getSimpleName(), path);
                                    if (Constants.state == 1) {
                                        activity.resumeMovie();
                                    } else {
                                        activity.start(path);
                                        activity.startMovieFromNative(path);
                                    }
                                    Constants.state = 0;
                                }
                            }
                        });
                        String message = client.listen();
                        return message;
                    }
                })
                .filter(new Func1<String, Boolean>() {
                    @Override
                    public Boolean call(String message) {
                        return message.split(".").length != 4;
                    }
                })
                .subscribeOn(AndroidSchedulers.mainThread())
                .subscribe(new Action1<String>() {
                    @Override
                    public void call(String message) {
                        //
                    }
                });
    }

    // 02 03
    public void recieveFile(String ip) {
        Observable.just(ip)
                .filter(new Func1<String, Boolean>() {
                    @Override
                    public Boolean call(String ip) {
                        return !TextUtils.isEmpty(ip);
                    }
                })
                .observeOn(Schedulers.io())
                .flatMap(new Func1<String, Observable<Client02>>() {
                    @Override
                    public Observable<Client02> call(String ip) {
                        Client02 client = new Client02();
                        client.setIp(ip);
                        client.getClientNum();
                        sendMessage(ip);
                        int fileCount = client.getFileInfo();
                        for (int i = 0; i < fileCount; i++) {
                            Log.w("Client02", "recieveFile " + i);
                            client.getMediaInfo();
                            client.recieveFile();
                        }
                        return Observable.just(client);
                    }
                })
                .subscribeOn(AndroidSchedulers.mainThread())
                .subscribe(new Action1<Client02>() {
                    @Override
                    public void call(Client02 client02) {
//                        Client03 client = new Client03();
//                        client.setIp(ip);
//                        client.sendMessage(getActivity());
                    }
                }, new Action1<Throwable>() {
                    @Override
                    public void call(Throwable throwable) {

                    }
                });
    }

    public void sendMessage(String ip) {
        Observable.just(ip)
                .filter(new Func1<String, Boolean>() {
                    @Override
                    public Boolean call(String s) {
                        return !TextUtils.isEmpty(s);
                    }
                })
                .subscribeOn(Schedulers.newThread())
                .subscribe(new Action1<String>() {
                    @Override
                    public void call(String ip) {
                        Client03 client = new Client03();
                        client.setIp(ip);
                        client.sendMessage(getBaseContext());
                    }
                }, new Action1<Throwable>() {
                    @Override
                    public void call(Throwable throwable) {
                        throwable.toString();
                    }
                });
    }

    public Activity getCurrentShowActivity() {
        return mCurrentActivity.get();
    }

    public static Context getContext() {
        return instance.getApplicationContext();
    }
}