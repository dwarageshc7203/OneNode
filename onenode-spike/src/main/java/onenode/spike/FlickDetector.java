package onenode.spike;

public final class FlickDetector {
    private static final int N = 256, MASK = N - 1;
    private static final long WINDOW_NS = 120_000_000L, MIN_DT_NS = 10_000_000L, COOLDOWN_NS = 800_000_000L;
    private final long[] t = new long[N];
    private final int[] xs = new int[N], ys = new int[N];
    private int head, size;
    private long cooldownUntil = Long.MIN_VALUE;
    private final int edgeStart;
    private final double minVx;

    public FlickDetector(int screenWidth, int edgeZonePx, double minVxPxPerSec) {
        this.edgeStart = screenWidth - edgeZonePx;
        this.minVx = minVxPxPerSec;
    }

    public boolean onSample(long tNs, int x, int y) {
        t[head] = tNs;
        xs[head] = x;
        ys[head] = y;
        head = (head + 1) & MASK;
        if (size < N) size++;
        
        if (tNs < cooldownUntil || x < edgeStart) return false;
        
        int oldest = -1;
        for (int i = 0; i < size; i++) {
            int idx = (head - 1 - i) & MASK;
            if (tNs - t[idx] > WINDOW_NS) break;
            oldest = idx;
        }
        if (oldest < 0 || tNs - t[oldest] < MIN_DT_NS) return false;
        
        double dt = (tNs - t[oldest]) / 1e9;
        double vx = (x - xs[oldest]) / dt;
        double vy = (y - ys[oldest]) / dt;
        
        if (vx >= minVx && Math.abs(vy) < 0.5 * vx) {
            cooldownUntil = tNs + COOLDOWN_NS;
            size = 0;
            return true;
        }
        return false;
    }
}
