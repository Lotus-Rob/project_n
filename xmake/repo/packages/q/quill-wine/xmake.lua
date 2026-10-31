-- Quill asks Windows whether a log file's directory is case-sensitive and throws unless the call
-- succeeds or reports itself unsupported. Wine, and so Proton, answers ERROR_CALL_NOT_IMPLEMENTED,
-- which isn't on that list, so no file sink could be created there. Upstream candidate for Quill.
package("quill-wine")
    set_base("quill")
    on_install("windows", "linux", "macosx", "mingw", function (package)
        local header = "include/quill/sinks/FileSink.h"
        local tolerated = "error != ERROR_INVALID_FUNCTION)"
        -- io.replace changes nothing, silently, once a Quill bump rewords the line: fail instead.
        if not io.readfile(header):find(tolerated, 1, true) then
            raise("quill-wine: %s no longer contains '%s'; update the patch for this Quill version", header, tolerated)
        end
        io.replace(header, tolerated, "error != ERROR_INVALID_FUNCTION && error != ERROR_CALL_NOT_IMPLEMENTED)", {plain = true})
        package:base():script("install")(package)
    end)
