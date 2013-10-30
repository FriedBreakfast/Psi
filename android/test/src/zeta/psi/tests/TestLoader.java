package zeta.psi.tests;

import junit.framework.Test;
import junit.framework.TestCase;
import junit.framework.TestSuite;
import android.test.InstrumentationTestRunner;

public class TestLoader extends InstrumentationTestRunner {
  static {
    System.loadLibrary("psi-combined");
    System.loadLibrary("psi-tvm-test");
  }
  
  private native static void buildPsiTests(TestSuite s);
  private native static void runPsiTest(long address);
  
  @Override
  public TestSuite getAllTests() {
    TestSuite s = new TestSuite();
    buildPsiTests(s);
    return s;
  }
  
  private static class PsiTestRunner extends TestCase {
    private long address;
    
    public PsiTestRunner(String name, long address) {
      super(name);
      this.address = address;
    }
    
    protected void runTest() {
      runPsiTest(address);
    }
  };
  
  private static void addPsiTest(TestSuite suite, String testName, long address) {
    suite.addTest(new PsiTestRunner(testName, address));
  }
};
