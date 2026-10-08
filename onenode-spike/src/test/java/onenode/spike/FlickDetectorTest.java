package onenode.spike;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class FlickDetectorTest {

    @Test
    public void testSlowDrift_noFire() {
        FlickDetector detector = new FlickDetector(1920, 24, 3000.0);
        
        assertFalse(detector.onSample(100_000_000L, 1850, 500));
        assertFalse(detector.onSample(150_000_000L, 1870, 500)); 
        assertFalse(detector.onSample(200_000_000L, 1890, 500)); 
        assertFalse(detector.onSample(220_000_000L, 1910, 500)); 
    }

    @Test
    public void testFastFlick_fires() {
        FlickDetector detector = new FlickDetector(1920, 24, 3000.0);
        
        assertFalse(detector.onSample(100_000_000L, 1850, 500));
        assertTrue(detector.onSample(120_000_000L, 1919, 500)); // dt=20ms, dx=69, vx=3450 > 3000
    }

    @Test
    public void testJumpToClampedEdge_fires() {
        FlickDetector detector = new FlickDetector(1920, 24, 3000.0);
        // This tests the exact case where cursor enters the edge zone in 1 event
        assertFalse(detector.onSample(100_000_000L, 1750, 500)); // 170px away, dt=0
        assertFalse(detector.onSample(120_000_000L, 1780, 500)); // 140px away, dt=20ms
        assertTrue(detector.onSample(140_000_000L, 1919, 500));  // hits edge, dx=139 from prev, vx=6950, dx=169 from first, vx=4225
    }

    @Test
    public void testDiagonalFlick_noFire() {
        FlickDetector detector = new FlickDetector(1920, 24, 3000.0);
        
        assertFalse(detector.onSample(100_000_000L, 1850, 500));
        assertFalse(detector.onSample(120_000_000L, 1919, 600)); // dy=100 (vy > 0.5*vx)
    }

    @Test
    public void testCooldown() {
        FlickDetector detector = new FlickDetector(1920, 24, 3000.0);
        
        assertFalse(detector.onSample(100_000_000L, 1850, 500));
        assertTrue(detector.onSample(120_000_000L, 1919, 500)); // Fires!

        // Now within 800ms cooldown
        assertFalse(detector.onSample(200_000_000L, 1850, 500));
        assertFalse(detector.onSample(220_000_000L, 1919, 500)); // Should not fire
        
        // After cooldown
        assertFalse(detector.onSample(1_000_000_000L, 1850, 500));
        assertTrue(detector.onSample(1_020_000_000L, 1919, 500)); // Fires!
    }
}
