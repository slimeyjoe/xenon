import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.*;
import ghidra.program.model.mem.*;
import java.math.BigInteger;
public class SetDS extends GhidraScript {
  public void run() throws Exception {
    Register ds = currentProgram.getRegister("ds");
    for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
      if (b.isExecute() || b.isInitialized())
        currentProgram.getProgramContext().setValue(ds, b.getStart(), b.getEnd(), BigInteger.valueOf(0x1f9f));
      println(b.getName()+" "+b.getStart()+"-"+b.getEnd()+" x="+b.isExecute());
    }
  }
}
