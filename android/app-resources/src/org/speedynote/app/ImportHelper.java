package org.speedynote.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import android.content.ClipData;

/**
 * Helper class for picking and importing .3enotes project files and legacy .snbx packages on Android.
 * 
 * This class handles the Storage Access Framework (SAF) properly by:
 * 1. Opening the file picker for .3enotes and .snbx files
 * 2. Copying the file to local storage while the temporary permission is valid
 * 3. Returning the local file path to C++
 * 
 * Part of the Share/Import feature (Phase 2).
 * 
 * Similar pattern to PdfFileHelper, but for notebook packages.
 */
public class ImportHelper {
    private static final String TAG = "ImportHelper";
    private static final int REQUEST_CODE_PICK_SNBX = 9002;
    
    private static Activity sActivity;
    private static String sPendingDestDir;
    private static String sLastViewUri;
    
    // Native callbacks to C++
    private static native void onPackageFilePicked(String localPath);
    private static native void onPackageFilesPicked(String[] localPaths);  // Multi-file
    private static native void onPackagePickCancelled();
    
    /**
     * Opens the system file picker to select one or more .snbx packages.
     * Called from C++ via JNI.
     * 
     * @param activity The current Activity
     * @param destDir Directory to copy the .snbx file(s) to (for extraction)
     */
    public static void pickPackageFile(Activity activity, String destDir) {
        sActivity = activity;
        sPendingDestDir = destDir;
        
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        
        // Accept any file type since .snbx is a custom extension
        // Android doesn't have a registered MIME type for .snbx
        intent.setType("*/*");
        
        // Enable multi-file selection (Phase 3: Batch Import)
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        
        // These flags grant read permission that persists until the Activity result is processed
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        
        Log.d(TAG, "Opening file picker for .3enotes projects (multi-select), destDir=" + destDir);
        activity.startActivityForResult(intent, REQUEST_CODE_PICK_SNBX);
    }
    

    /**
     * Handle a .3enotes file opened from Android Files, email, browser, or
     * another application. The URI is copied into app-private storage before
     * the same native import callback used by the in-app picker is invoked.
     */
    public static boolean handleViewIntent(Activity activity, Intent intent) {
        if (activity == null || intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) {
            return false;
        }

        Uri uri = intent.getData();
        if (uri == null) return false;

        String uriKey = uri.toString();
        if (uriKey.equals(sLastViewUri)) {
            Log.d(TAG, "Ignoring duplicate VIEW intent: " + uriKey);
            return true;
        }

        String filename = getFileName(activity, uri);
        String lower = filename == null ? "" : filename.toLowerCase();
        String mime = intent.getType();
        boolean supported = lower.endsWith(".3enotes") || lower.endsWith(".snbx")
                || "application/x-3enotes".equals(mime);
        if (!supported) {
            Log.w(TAG, "Unsupported VIEW intent file: " + filename + " mime=" + mime);
            return false;
        }

        File importsDir = new File(activity.getFilesDir(), "imports");
        String localPath = copyUriToLocal(activity, uri, importsDir.getAbsolutePath());
        if (localPath == null) {
            Log.e(TAG, "Could not import VIEW intent: " + uriKey);
            return true;
        }

        try {
            sLastViewUri = uriKey;
            onPackageFilePicked(localPath);
            Log.d(TAG, "Imported VIEW intent to: " + localPath);
        } catch (UnsatisfiedLinkError error) {
            // Qt may still be loading during a cold start. Leave the URI
            // unmarked so SpeedyNoteActivity can retry shortly.
            Log.w(TAG, "Native importer not ready yet; will retry", error);
            new File(localPath).delete();
            return false;
        }
        return true;
    }

    /**
     * Called from SpeedyNoteActivity's onActivityResult.
     * Must be called from the UI thread.
     * 
     * Handles both single-file and multi-file selection results.
     * 
     * @return true if this was our request and we handled it, false otherwise
     */
    public static boolean handleActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_CODE_PICK_SNBX) {
            return false; // Not our request
        }
        
        if (resultCode != Activity.RESULT_OK || data == null) {
            Log.d(TAG, "Package pick cancelled");
            onPackagePickCancelled();
            clearStaticReferences();
            return true;
        }
        
        // Collect all URIs (multi-select or single-select)
        ArrayList<Uri> uris = new ArrayList<>();
        
        // Check for multi-file selection first
        ClipData clipData = data.getClipData();
        if (clipData != null) {
            // Multiple files selected
            for (int i = 0; i < clipData.getItemCount(); i++) {
                Uri uri = clipData.getItemAt(i).getUri();
                if (uri != null) {
                    uris.add(uri);
                }
            }
            Log.d(TAG, "Multi-select: " + uris.size() + " files");
        } else {
            // Single file selected
            Uri uri = data.getData();
            if (uri != null) {
                uris.add(uri);
                Log.d(TAG, "Single-select: 1 file");
            }
        }
        
        if (uris.isEmpty()) {
            Log.e(TAG, "No URIs in result");
            onPackagePickCancelled();
            clearStaticReferences();
            return true;
        }
        
        // Copy all files to local storage
        ArrayList<String> localPaths = new ArrayList<>();
        
        for (Uri uri : uris) {
            Log.d(TAG, "Processing URI: " + uri.toString());
            
            // Validate that the file looks like a .snbx package
            String filename = getFileName(sActivity, uri);
            if (filename == null || !(filename.toLowerCase().endsWith(".3enotes") || filename.toLowerCase().endsWith(".snbx"))) {
                Log.w(TAG, "Selected file is not a 3ENotes project: " + filename + ", skipping");
                continue;  // Skip unsupported files
            }
            
            // Copy file to local storage while permission is still valid
            String localPath = copyUriToLocal(sActivity, uri, sPendingDestDir);
            
            if (localPath != null) {
                Log.d(TAG, "Successfully copied to: " + localPath);
                localPaths.add(localPath);
            } else {
                Log.e(TAG, "Failed to copy package file: " + filename);
            }
        }
        
        // Call appropriate callback based on result count
        if (localPaths.isEmpty()) {
            Log.e(TAG, "No 3ENotes project files were successfully copied");
            onPackagePickCancelled();
        } else if (localPaths.size() == 1) {
            // Single file - use original callback for compatibility
            onPackageFilePicked(localPaths.get(0));
        } else {
            // Multiple files - use new batch callback
            String[] pathArray = localPaths.toArray(new String[0]);
            onPackageFilesPicked(pathArray);
        }
        
        clearStaticReferences();
        return true;
    }
    
    /**
     * Clear static references to avoid memory leaks.
     */
    private static void clearStaticReferences() {
        sActivity = null;
        sPendingDestDir = null;
    }
    
    /**
     * Copies a content:// URI to local storage.
     * This MUST be called while the SAF permission is still valid.
     */
    private static String copyUriToLocal(Context context, Uri uri, String destDir) {
        if (context == null || uri == null || destDir == null) {
            return null;
        }
        
        // Get the original filename
        String filename = getFileName(context, uri);
        if (filename == null || filename.isEmpty()) {
            filename = "imported_" + System.currentTimeMillis() + ".3enotes";
        }
        
        // Preserve legacy .snbx names; otherwise use the new portable extension.
        String lowerName = filename.toLowerCase();
        if (!(lowerName.endsWith(".3enotes") || lowerName.endsWith(".snbx"))) {
            filename = filename + ".3enotes";
        }
        
        // Ensure destination directory exists
        File destDirFile = new File(destDir);
        if (!destDirFile.exists()) {
            if (!destDirFile.mkdirs()) {
                Log.e(TAG, "Failed to create destination directory: " + destDir);
                return null;
            }
        }
        
        // Generate unique filename if needed
        File destFile = new File(destDir, filename);
        if (destFile.exists()) {
            int dotIndex = filename.lastIndexOf('.');
            String baseName = dotIndex > 0 ? filename.substring(0, dotIndex) : filename;
            String extension = dotIndex > 0 ? filename.substring(dotIndex) : ".3enotes";
            int counter = 1;
            while (destFile.exists()) {
                filename = baseName + "_" + counter + extension;
                destFile = new File(destDir, filename);
                counter++;
                
                // Safety limit
                if (counter > 1000) {
                    Log.e(TAG, "Too many duplicate filenames");
                    return null;
                }
            }
            Log.d(TAG, "Using unique filename: " + filename);
        }
        
        // Copy the file
        ContentResolver resolver = context.getContentResolver();
        InputStream inputStream = null;
        OutputStream outputStream = null;
        
        try {
            inputStream = resolver.openInputStream(uri);
            if (inputStream == null) {
                Log.e(TAG, "Failed to open input stream");
                return null;
            }
            
            outputStream = new FileOutputStream(destFile);
            
            byte[] buffer = new byte[65536]; // 64KB buffer
            int bytesRead;
            long totalBytes = 0;
            
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
                totalBytes += bytesRead;
            }
            
            Log.d(TAG, "Copied " + totalBytes + " bytes to " + destFile.getAbsolutePath());
            return destFile.getAbsolutePath();
            
        } catch (Exception e) {
            Log.e(TAG, "Error copying file: " + e.getMessage());
            e.printStackTrace();
            // Clean up partial file
            if (destFile.exists()) {
                destFile.delete();
            }
            return null;
        } finally {
            try {
                if (inputStream != null) inputStream.close();
                if (outputStream != null) outputStream.close();
            } catch (Exception e) {
                // Ignore close errors
            }
        }
    }
    
    /**
     * Gets the display name of a content:// URI.
     */
    private static String getFileName(Context context, Uri uri) {
        String result = null;
        
        if ("content".equals(uri.getScheme())) {
            Cursor cursor = null;
            try {
                cursor = context.getContentResolver().query(uri, null, null, null, null);
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        result = cursor.getString(index);
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Error getting filename: " + e.getMessage());
            } finally {
                if (cursor != null) {
                    cursor.close();
                }
            }
        }
        
        if (result == null) {
            result = uri.getLastPathSegment();
        }
        
        return result;
    }
}

