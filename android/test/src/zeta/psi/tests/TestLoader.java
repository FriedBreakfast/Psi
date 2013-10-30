package zeta.psi.tests;

import junit.framework.Test;
import junit.framework.TestCase;
import junit.framework.TestSuite;

public class TestLoader extends TestCase {
  static {
    System.loadLibrary("psi-combined");
    System.loadLibrary("psi-tvm-test");
  }
  
  private native static void buildSuite(TestSuite s);
  
  public static Test suite() {
    TestSuite s = new TestSuite();
    buildSuite(s);
    return s;
  }
};
