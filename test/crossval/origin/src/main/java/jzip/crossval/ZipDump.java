package jzip.crossval;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import software.coley.lljzip.ZipIO;
import software.coley.lljzip.format.compression.ZipCompressions;
import software.coley.lljzip.format.model.*;
import software.coley.lljzip.util.MemorySegmentUtil;

import java.io.*;
import java.lang.foreign.MemorySegment;
import java.nio.file.*;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class ZipDump {

    private static final Set<String> STRATEGIES = Set.of("jvm", "standard", "naive", "zipfile");

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: ZipDump <testResourceDir> <goldenDir> [strategy] [sample]");
            System.err.println("  strategy: jvm | standard | naive | zipfile | all (default: all)");
            System.err.println("  sample: specific filename, or 'all' (default: all)");
            System.exit(1);
        }

        Path testResourceDir = Path.of(args[0]);
        Path goldenDir = Path.of(args[1]);
        String strategyFilter = args.length > 2 ? args[2] : "all";
        String sampleFilter = args.length > 3 ? args[3] : "all";

        Files.createDirectories(goldenDir);

        List<Path> samples = collectSamples(testResourceDir, sampleFilter);
        if (samples.isEmpty()) {
            System.err.println("No samples found in: " + testResourceDir);
            System.exit(1);
        }

        System.out.println("Found " + samples.size() + " sample(s)");

        for (Path sample : samples) {
            String name = sample.getFileName().toString();
            System.out.println("  Processing: " + name);

            for (String strategy : STRATEGIES) {
                if (!strategyFilter.equals("all") && !strategyFilter.equals(strategy)) {
                    continue;
                }

                String dumpFileName = name + "." + strategy + ".dump";
                Path dumpPath = goldenDir.resolve(dumpFileName);

                try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(dumpPath))) {
                    dumpSample(out, sample, strategy);
                } catch (Exception ex) {
                    System.err.println("    FAILED [" + strategy + "]: " + ex.getMessage());
                    Path errorPath = goldenDir.resolve(dumpFileName + ".error");
                    Files.writeString(errorPath, ex.getClass().getName() + ": " + ex.getMessage() + "\n");
                }
            }
        }

        System.out.println("Done. Golden files written to: " + goldenDir);
    }

    private static List<Path> collectSamples(Path dir, String filter) throws IOException {
        if (!filter.equals("all")) {
            Path specific = dir.resolve(filter);
            if (Files.exists(specific)) return List.of(specific);
            System.err.println("Sample not found: " + filter);
            return List.of();
        }

        List<Path> result = new ArrayList<>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(dir)) {
            for (Path entry : stream) {
                if (Files.isRegularFile(entry)) {
                    String name = entry.getFileName().toString();
                    if (name.endsWith(".jar") || name.endsWith(".zip") || name.endsWith(".png")) {
                        result.add(entry);
                    }
                }
            }
        }
        result.sort(Comparator.comparing(p -> p.getFileName().toString()));
        return result;
    }

    private static void dumpSample(PrintWriter out, Path samplePath, String strategy) throws Exception {
        out.println("ARCHIVE: " + samplePath.getFileName());
        out.println("STRATEGY: " + strategy);

        switch (strategy) {
            case "jvm" -> dumpLljzip(out, samplePath, "jvm");
            case "standard" -> dumpLljzip(out, samplePath, "standard");
            case "naive" -> dumpLljzip(out, samplePath, "naive");
            case "zipfile" -> dumpZipFile(out, samplePath);
        }
    }

    private static void dumpLljzip(PrintWriter out, Path samplePath, String strategy) throws Exception {
        ZipArchive archive = switch (strategy) {
            case "jvm" -> ZipIO.readJvm(samplePath);
            case "standard" -> ZipIO.readStandard(samplePath);
            case "naive" -> ZipIO.readNaive(samplePath);
            default -> throw new IllegalArgumentException(strategy);
        };

        try (archive) {
            var locals = archive.getLocalFiles();
            var centrals = archive.getCentralDirectories();
            var end = archive.getEnd();

            out.println("LOCAL_COUNT: " + locals.size());
            out.println("CENTRAL_COUNT: " + centrals.size());

            if (end != null) {
                out.println("EOCD:");
                out.println("  OFFSET: " + end.offset());
                out.println("  DISK_NUMBER: " + end.getDiskNumber());
                out.println("  CEN_START_DISK: " + end.getCentralDirectoryStartDisk());
                out.println("  CEN_START_OFFSET: " + end.getCentralDirectoryStartOffset());
                out.println("  NUM_ENTRIES: " + end.getNumEntries());
                out.println("  CEN_SIZE: " + end.getCentralDirectorySize());
                out.println("  CEN_OFFSET: " + end.getCentralDirectoryOffset());
            } else {
                out.println("EOCD: null");
            }

            out.println();
            out.println("--- LOCALS ---");
            for (LocalFileHeader lfh : locals) {
                dumpLocalFileHeader(out, lfh);
            }

            out.println();
            out.println("--- CENTRALS ---");
            for (CentralDirectoryFileHeader cen : centrals) {
                dumpCentralDirectoryFileHeader(out, cen);
            }
        }
    }

    private static void dumpLocalFileHeader(PrintWriter out, LocalFileHeader lfh) {
        String name = lfh.getFileNameAsString();
        out.println("ENTRY: " + name);
        out.println("  OFFSET: " + lfh.offset());
        out.println("  VERSION: " + lfh.getVersionNeededToExtract());
        out.println("  GP_FLAG: 0x" + String.format("%04X", lfh.getGeneralPurposeBitFlag()));
        out.println("  METHOD: " + lfh.getCompressionMethod());
        out.println("  MOD_TIME: 0x" + String.format("%04X", lfh.getLastModFileTime()));
        out.println("  MOD_DATE: 0x" + String.format("%04X", lfh.getLastModFileDate()));
        out.println("  CRC32: 0x" + String.format("%08X", lfh.getCrc32()));
        out.println("  COMPRESSED_SIZE: " + lfh.getCompressedSize());
        out.println("  UNCOMPRESSED_SIZE: " + lfh.getUncompressedSize());
        out.println("  FILE_NAME_LENGTH: " + lfh.getFileNameLength());
        out.println("  EXTRA_FIELD_LENGTH: " + lfh.getExtraFieldLength());

        MemorySegment fileData = lfh.getFileData();
        out.println("  FILE_DATA_LENGTH: " + (fileData != null ? fileData.byteSize() : -1));

        boolean differs = lfh.hasDifferentValuesThanCentralDirectoryHeader();
        if (differs) {
            out.println("  DIFFERS_FROM_CENTRAL: true");
        }

        if (fileData != null && fileData.byteSize() > 0) {
            dumpDataIntegrity(out, name, lfh);
        }
    }

    private static void dumpCentralDirectoryFileHeader(PrintWriter out, CentralDirectoryFileHeader cen) {
        String name = cen.getFileNameAsString();
        out.println("ENTRY: " + name);
        out.println("  OFFSET: " + cen.offset());
        out.println("  VERSION_MADE_BY: " + cen.getVersionMadeBy());
        out.println("  VERSION_NEEDED: " + cen.getVersionNeededToExtract());
        out.println("  GP_FLAG: 0x" + String.format("%04X", cen.getGeneralPurposeBitFlag()));
        out.println("  METHOD: " + cen.getCompressionMethod());
        out.println("  MOD_TIME: 0x" + String.format("%04X", cen.getLastModFileTime()));
        out.println("  MOD_DATE: 0x" + String.format("%04X", cen.getLastModFileDate()));
        out.println("  CRC32: 0x" + String.format("%08X", cen.getCrc32()));
        out.println("  COMPRESSED_SIZE: " + cen.getCompressedSize());
        out.println("  UNCOMPRESSED_SIZE: " + cen.getUncompressedSize());
        out.println("  FILE_NAME_LENGTH: " + cen.getFileNameLength());
        out.println("  EXTRA_FIELD_LENGTH: " + cen.getExtraFieldLength());
        out.println("  DISK_NUMBER_START: " + cen.getDiskNumberStart());
        out.println("  INTERNAL_ATTRS: " + cen.getInternalFileAttributes());
        out.println("  EXTERNAL_ATTRS: 0x" + String.format("%08X", cen.getExternalFileAttributes()));
        out.println("  LOCAL_HEADER_OFFSET: " + cen.getRelativeOffsetOfLocalHeader());

        if (cen.hasZip64RelativeOffsetOfLocalHeader()) {
            out.println("  ZIP64_OFFSET: true");
        }
    }

    private static void dumpDataIntegrity(PrintWriter out, String name, LocalFileHeader lfh) {
        try {
            MemorySegment decompressed = ZipCompressions.decompress(lfh);
            byte[] bytes = MemorySegmentUtil.toByteArray(decompressed);

            if (name.endsWith(".class")) {
                dumpClassValidation(out, name, bytes);
            } else if (name.endsWith(".jar")) {
                out.println("  INNER_JAR: parseable");
            } else {
                out.println("  DATA_INTEGRITY: OK");
            }
        } catch (Exception ex) {
            out.println("  DATA_INTEGRITY: FAIL(" + ex.getClass().getSimpleName() + ": " + ex.getMessage() + ")");
        }
    }

    private static void dumpClassValidation(PrintWriter out, String name, byte[] bytes) {
        if (bytes.length < 4 || bytes[0] != (byte) 0xCA || bytes[1] != (byte) 0xFE ||
            bytes[2] != (byte) 0xBA || bytes[3] != (byte) 0xBE) {
            out.println("  CLASS_VALIDATION: MAGIC_FAIL");
            return;
        }

        try {
            List<String> ldcConstants = new ArrayList<>();
            ClassReader reader = new ClassReader(bytes);
            reader.accept(new ClassVisitor(Opcodes.ASM9) {
                @Override
                public MethodVisitor visitMethod(int access, String methodName, String descriptor,
                                                  String signature, String[] exceptions) {
                    return new MethodVisitor(Opcodes.ASM9) {
                        @Override
                        public void visitLdcInsn(Object value) {
                            if (value instanceof String s) {
                                ldcConstants.add(s);
                            }
                        }
                    };
                }
            }, ClassReader.SKIP_FRAMES);

            out.println("  CLASS_VALIDATION: ASM_OK");
            if (!ldcConstants.isEmpty()) {
                // URL-encode LDC constants to avoid binary data breaking the dump format
                List<String> encoded = new ArrayList<>();
                for (String s : ldcConstants) {
                    StringBuilder sb = new StringBuilder();
                    for (byte b : s.getBytes(java.nio.charset.StandardCharsets.UTF_8)) {
                        int val = b & 0xFF;
                        if (val >= 32 && val < 127 && val != '|' && val != '%') {
                            sb.append((char) val);
                        } else {
                            sb.append(String.format("%%%02X", val));
                        }
                    }
                    encoded.add(sb.toString());
                }
                out.println("  LDC_CONSTANTS: " + String.join("|", encoded));
            }
        } catch (Exception ex) {
            out.println("  CLASS_VALIDATION: ASM_FAIL(" + ex.getClass().getSimpleName() + ": " + ex.getMessage() + ")");
        }
    }

    private static void dumpZipFile(PrintWriter out, Path samplePath) throws Exception {
        try (ZipFile zf = new ZipFile(samplePath.toFile())) {
            Enumeration<? extends ZipEntry> entries = zf.entries();
            List<ZipEntry> entryList = new ArrayList<>();
            while (entries.hasMoreElements()) {
                entryList.add(entries.nextElement());
            }

            out.println("ENTRY_COUNT: " + entryList.size());
            out.println();

            for (ZipEntry entry : entryList) {
                out.println("ENTRY: " + entry.getName());
                out.println("  METHOD: " + entry.getMethod());
                out.println("  SIZE: " + entry.getSize());
                out.println("  COMPRESSED_SIZE: " + entry.getCompressedSize());
                out.println("  CRC32: 0x" + String.format("%08X", entry.getCrc()));
                out.println("  IS_DIRECTORY: " + entry.isDirectory());
            }
        }
    }
}
