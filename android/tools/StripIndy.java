import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.AbstractInsnNode;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.InsnList;
import org.objectweb.asm.tree.InsnNode;
import org.objectweb.asm.tree.InvokeDynamicInsnNode;
import org.objectweb.asm.tree.LdcInsnNode;
import org.objectweb.asm.tree.MethodInsnNode;
import org.objectweb.asm.tree.MethodNode;
import org.objectweb.asm.tree.TypeInsnNode;

import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;

/**
 * dx cannot desugar lambdas and ART has no LambdaMetafactory, so every method
 * in a class tree that still contains an invokedynamic has its body replaced by
 * "throw UnsupportedOperationException". The app is checked separately never to
 * call these methods; this only keeps invoke-custom out of the dex entirely.
 * Usage: java -cp asm.jar:asm-tree.jar:. StripIndy <classes dir>
 */
public class StripIndy {
    static int methods = 0;

    public static void main(String[] args) throws IOException {
        Path root = Paths.get(args[0]);
        Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                if (file.toString().endsWith(".class")) strip(file);
                return FileVisitResult.CONTINUE;
            }
        });
        System.out.println("stripped " + methods + " method(s)");
    }

    static void strip(Path file) throws IOException {
        byte[] in = Files.readAllBytes(file);
        ClassNode cn = new ClassNode();
        new ClassReader(in).accept(cn, 0);
        boolean changed = false;
        for (MethodNode m : cn.methods) {
            boolean indy = false;
            for (AbstractInsnNode n = m.instructions.getFirst(); n != null; n = n.getNext()) {
                if (n instanceof InvokeDynamicInsnNode) { indy = true; break; }
            }
            if (!indy) continue;
            InsnList body = new InsnList();
            body.add(new TypeInsnNode(Opcodes.NEW, "java/lang/UnsupportedOperationException"));
            body.add(new InsnNode(Opcodes.DUP));
            body.add(new LdcInsnNode(cn.name.replace('/', '.') + "." + m.name + " is not available in this build"));
            body.add(new MethodInsnNode(Opcodes.INVOKESPECIAL, "java/lang/UnsupportedOperationException",
                    "<init>", "(Ljava/lang/String;)V", false));
            body.add(new InsnNode(Opcodes.ATHROW));
            m.instructions = body;
            m.tryCatchBlocks.clear();
            if (m.localVariables != null) m.localVariables.clear();
            m.visibleLocalVariableAnnotations = null;
            m.invisibleLocalVariableAnnotations = null;
            m.maxStack = 3;
            methods++;
            changed = true;
            System.out.println("  " + cn.name + "." + m.name + m.desc);
        }
        if (!changed) return;
        ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_MAXS);
        cn.accept(cw);
        Files.write(file, cw.toByteArray());
    }
}
