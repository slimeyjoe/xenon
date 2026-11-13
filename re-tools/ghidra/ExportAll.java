import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class ExportAll extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    PrintWriter c = new PrintWriter(new FileWriter(out + "/ags_decomp.c"));
    PrintWriter l = new PrintWriter(new FileWriter(out + "/ags_functions.tsv"));
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
    while (it.hasNext()) {
      Function f = it.next();
      l.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + f.getSignature().getPrototypeString());
      DecompileResults r = di.decompileFunction(f, 120, monitor);
      c.println("// ==== " + f.getEntryPoint() + " " + f.getName());
      if (r != null && r.decompileCompleted()) c.println(r.getDecompiledFunction().getC());
      else c.println("// decompile failed: " + (r==null?"null":r.getErrorMessage()));
    }
    c.close(); l.close();
    PrintWriter s = new PrintWriter(new FileWriter(out + "/ags_strings.tsv"));
    DataIterator dit = currentProgram.getListing().getDefinedData(true);
    while (dit.hasNext()) { Data d = dit.next(); if (d.hasStringValue()) s.println(d.getAddress()+"\t"+d.getValue()); }
    s.close();
  }
}
