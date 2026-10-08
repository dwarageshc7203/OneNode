package onenode.spike;

import com.sun.jna.Pointer;
import com.sun.jna.platform.win32.User32;
import com.sun.jna.platform.win32.WinDef.HWND;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.scene.Scene;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.shape.Circle;
import javafx.stage.Stage;
import javafx.stage.StageStyle;

import java.util.Arrays;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.locks.LockSupport;

public class SpikeApp extends Application {
    
    private MouseHook mouseHook;
    private FlickDetector flickDetector;
    private HWND hwnd;
    private Stage mainStage;
    
    private static final int WS_EX_NOACTIVATE = 0x08000000;
    private static final int WS_EX_TOOLWINDOW = 0x00000080;
    private static final int GWL_EXSTYLE = -20;
    private static final int SWP_NOACTIVATE = 0x0010;
    private static final int SWP_NOMOVE = 0x0002;
    private static final int SWP_NOSIZE = 0x0001;
    private static final int SWP_FRAMECHANGED = 0x0020;
    private static final int HWND_TOPMOST = -1;
    private static final int SM_CXSCREEN = 0;
    private static final int SW_SHOWNOACTIVATE = 4;
    private static final int SW_HIDE = 0;
    
    private long lastTriggerNs = 0;
    private boolean isDragActive = false;
    private final Timer globalTimer = new Timer(true);
    private long lastShowTime = 0;
    
    // Toggle for testing native ShowWindow vs JavaFX show
    private static final boolean USE_NATIVE_SHOW = false; 

    @Override
    public void start(Stage primaryStage) {
        Platform.setImplicitExit(false);
        mainStage = primaryStage;
        
        Circle circle = new Circle(40, Color.TEAL);
        StackPane root = new StackPane(circle);
        root.setStyle("-fx-background-color: rgba(200, 200, 200, 0.8); -fx-background-radius: 46;");
        root.setPrefSize(92, 200);
        
        root.setOnDragOver(event -> {
            isDragActive = true;
            if (event.getDragboard().hasFiles()) {
                event.acceptTransferModes(TransferMode.COPY);
                circle.setStroke(Color.WHITE);
                circle.setStrokeWidth(3);
                circle.setScaleX(1.12);
                circle.setScaleY(1.12);
            }
            event.consume();
        });
        
        root.setOnDragExited(event -> {
            isDragActive = false;
            circle.setStroke(null);
            circle.setScaleX(1.0);
            circle.setScaleY(1.0);
            event.consume();
        });
        
        root.setOnDragDropped(event -> {
            if (event.getDragboard().hasFiles()) {
                System.out.println("Dropped files: " + event.getDragboard().getFiles());
                logForegroundWindow("After Drop");
                event.setDropCompleted(true);
                Platform.runLater(this::hideWindowSafe);
            } else {
                event.setDropCompleted(false);
            }
            event.consume();
        });
        
        Scene scene = new Scene(root, 92, 200, Color.TRANSPARENT);
        primaryStage.setScene(scene);
        primaryStage.initStyle(StageStyle.TRANSPARENT);
        primaryStage.setAlwaysOnTop(true);
        
        String uniqueTitle = "OneNode Spike " + System.nanoTime();
        primaryStage.setTitle(uniqueTitle);
        
        primaryStage.setX(-10000);
        primaryStage.setY(-10000);
        primaryStage.setOpacity(0);
        primaryStage.show();
        
        hwnd = User32.INSTANCE.FindWindow(null, uniqueTitle);
        if (hwnd == null) {
            System.err.println("CRITICAL: FindWindow failed. Aborting spike.");
            System.exit(1);
        }
        
        int exStyle = User32.INSTANCE.GetWindowLong(hwnd, GWL_EXSTYLE);
        User32.INSTANCE.SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        User32.INSTANCE.SetWindowPos(hwnd, null, 0, 0, 0, 0, 
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        
        primaryStage.hide();
        primaryStage.setOpacity(1);
        
        double scaleX = javafx.stage.Screen.getPrimary().getOutputScaleX();
        int screenWidthPhysical = User32.INSTANCE.GetSystemMetrics(SM_CXSCREEN);
        double screenWidthScaled = screenWidthPhysical / scaleX;
        
        System.out.println("Expected physical width: " + screenWidthPhysical);
        System.out.println("Scaled width: " + screenWidthScaled);
        
        double finalX = screenWidthScaled - 92 - 16;
        double finalY = (javafx.stage.Screen.getPrimary().getBounds().getHeight() - 200) / 2.0;
        primaryStage.setX(finalX);
        primaryStage.setY(finalY);
        
        Thread detectorThread = new Thread(() -> {
            long readSeq = 0;
            while (true) {
                LockSupport.park();
                if (Thread.interrupted()) break;
                
                long w = mouseHook.writeSeq;
                long startSeq = Math.max(readSeq, w - 256);
                for (long s = startSeq; s < w; s++) {
                    int i = (int)(s & (MouseHook.RING_SIZE - 1));
                    if (flickDetector.onSample(mouseHook.timeRing[i], mouseHook.xRing[i], mouseHook.yRing[i])) {
                        lastTriggerNs = System.nanoTime();
                        logForegroundWindow("Before Show");
                        Platform.runLater(this::showWindowSafe);
                    }
                }
                readSeq = w;
            }
        });
        detectorThread.setDaemon(true);
        
        int edgeZonePx = (int)(24 * scaleX);
        flickDetector = new FlickDetector(screenWidthPhysical, edgeZonePx, 3000.0 * scaleX);
        mouseHook = new MouseHook(detectorThread, screenWidthPhysical, edgeZonePx);
        mouseHook.start();
        detectorThread.start();
        
        globalTimer.scheduleAtFixedRate(new TimerTask() {
            @Override
            public void run() {
                reportHookTimings();
                if (mainStage.isShowing()) {
                    logForegroundWindow("During Overlay Visible");
                    
                    if (!isDragActive && (System.currentTimeMillis() - lastShowTime > 3000)) {
                        Platform.runLater(() -> {
                            if (!mainStage.getScene().getRoot().isHover() && !isDragActive) {
                                hideWindowSafe();
                            }
                        });
                    }
                }
            }
        }, 1000, 1000);
    }
    
    private void showWindowSafe() {
        if (!mainStage.isShowing() || USE_NATIVE_SHOW) { // Simplified check to avoid IsWindowVisible JNA call issue
            lastShowTime = System.currentTimeMillis();
            if (USE_NATIVE_SHOW) {
                User32.INSTANCE.ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            } else {
                mainStage.show();
                User32.INSTANCE.SetWindowPos(hwnd, new HWND(Pointer.createConstant(HWND_TOPMOST)), 
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            
            Platform.runLater(() -> {
                long visibleNs = System.nanoTime();
                System.out.printf("Flick-to-visible latency: %.2f ms\n", (visibleNs - lastTriggerNs) / 1_000_000.0);
            });
        }
    }
    
    private void hideWindowSafe() {
        if (USE_NATIVE_SHOW) {
            User32.INSTANCE.ShowWindow(hwnd, SW_HIDE);
        } else {
            mainStage.hide();
        }
    }
    
    private void logForegroundWindow(String phase) {
        HWND fg = User32.INSTANCE.GetForegroundWindow();
        System.out.println(phase + " - Foreground HWND: " + (fg != null ? fg.toString() : "null"));
    }
    
    private void reportHookTimings() {
        long[] timings = mouseHook.hookTimingsNs;
        int count = 0;
        for (int i = 0; i < timings.length; i++) {
            if (timings[i] > 0) {
                count++;
            }
        }
        if (count == 0) return;
        long[] valid = new long[count];
        int idx = 0;
        for (int i = 0; i < timings.length; i++) {
            if (timings[i] > 0) {
                valid[idx++] = timings[i];
                timings[i] = 0; // Reset after reading
            }
        }
        Arrays.sort(valid);
        long p50 = valid[valid.length / 2];
        long p99 = valid[(int)(valid.length * 0.99)];
        long max = valid[valid.length - 1];
        System.out.printf("Hook Timings (ns): p50=%d, p99=%d, max=%d\n", p50, p99, max);
    }

    @Override
    public void stop() {
        if (mouseHook != null) {
            mouseHook.stop();
        }
        globalTimer.cancel();
    }

    public static void main(String[] args) {
        launch(args);
    }
}
