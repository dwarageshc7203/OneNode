package onenode.spike;

import com.sun.jna.Pointer;
import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.User32;
import com.sun.jna.platform.win32.WinDef;
import com.sun.jna.platform.win32.WinUser;
import java.util.concurrent.locks.LockSupport;

public class MouseHook {
    private static final int WH_MOUSE_LL = 14;
    private static final int WM_MOUSEMOVE = 0x0200;
    
    interface MouseProc extends WinUser.HOOKPROC {
        WinDef.LRESULT callback(int nCode, WinDef.WPARAM wParam, Pointer lParam);
    }
    
    private WinUser.HHOOK hhk;
    private final MouseProc mouseHookProc; // PREVENT GC
    private volatile boolean running = true;
    private Thread hookThread;
    private int hookThreadId = 0;
    
    public static final int RING_SIZE = 1024;
    public final long[] timeRing = new long[RING_SIZE];
    public final int[] xRing = new int[RING_SIZE];
    public final int[] yRing = new int[RING_SIZE];
    
    public volatile long writeSeq = 0;
    
    public final long[] hookTimingsNs = new long[10000];
    private int timingIndex = 0;

    public MouseHook(Thread workerThread, int screenWidth, int edgeZone) {
        this.mouseHookProc = (nCode, wParam, lParam) -> {
            long startNs = System.nanoTime();
            if (nCode >= 0 && wParam.intValue() == WM_MOUSEMOVE) {
                int x = lParam.getInt(0);
                int y = lParam.getInt(4);
                
                long w = writeSeq;
                int idx = (int)(w & (RING_SIZE - 1));
                timeRing[idx] = startNs;
                xRing[idx] = x;
                yRing[idx] = y;
                
                writeSeq = w + 1;
                
                if (x >= screenWidth - edgeZone) {
                    LockSupport.unpark(workerThread);
                }
            }
            long endNs = System.nanoTime();
            if (timingIndex < hookTimingsNs.length) {
                hookTimingsNs[timingIndex++] = (endNs - startNs);
            }
            return User32.INSTANCE.CallNextHookEx(hhk, nCode, wParam, new WinDef.LPARAM(Pointer.nativeValue(lParam)));
        };
    }

    public void start() {
        hookThread = new Thread(() -> {
            hookThreadId = Kernel32.INSTANCE.GetCurrentThreadId();
            WinDef.HMODULE hMod = Kernel32.INSTANCE.GetModuleHandle(null);
            hhk = User32.INSTANCE.SetWindowsHookEx(WH_MOUSE_LL, mouseHookProc, hMod, 0);

            int result;
            WinUser.MSG msg = new WinUser.MSG();
            while (running && (result = User32.INSTANCE.GetMessage(msg, null, 0, 0)) != 0) {
                if (result == -1) break;
                User32.INSTANCE.TranslateMessage(msg);
                User32.INSTANCE.DispatchMessage(msg);
            }
            if (hhk != null) {
                User32.INSTANCE.UnhookWindowsHookEx(hhk);
            }
        }, "MouseHook-Thread");
        hookThread.setDaemon(true);
        hookThread.start();
    }

    public void stop() {
        running = false;
        if (hookThreadId != 0) {
            User32.INSTANCE.PostThreadMessage(hookThreadId, WinUser.WM_QUIT, new WinDef.WPARAM(0), new WinDef.LPARAM(0));
        }
    }
}
