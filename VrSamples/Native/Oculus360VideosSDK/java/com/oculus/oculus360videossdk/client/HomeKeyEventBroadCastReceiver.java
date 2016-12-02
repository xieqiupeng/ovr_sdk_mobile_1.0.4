package com.oculus.oculus360videossdk.client;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Created by xieqi on 2016/12/1.
 */

public class HomeKeyEventBroadCastReceiver extends BroadcastReceiver {

    static final String SYSTEM_REASON = "reason";
    static final String SYSTEM_HOME_KEY = "homekey";//home key
    static final String SYSTEM_RECENT_APPS = "recentapps";//long home key

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (action.equals(Intent.ACTION_CLOSE_SYSTEM_DIALOGS)) {
            String reason = intent.getStringExtra(SYSTEM_REASON);
            if (reason.equals(SYSTEM_HOME_KEY)) {
                Log.w("HomeKey", "SYSTEM_HOME_KEY");
                //
            } else if (reason.equals(SYSTEM_RECENT_APPS)) {
                Log.w("HomeKey", "SYSTEM_RECENT_APPS");
            }
        }
    }
}
