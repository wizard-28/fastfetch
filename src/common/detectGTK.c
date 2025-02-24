#include "fastfetch.h"

#include <dlfcn.h>
#include <pthread.h>

static inline bool allPropertiesSet(FFGTKResult* result)
{
    return
        result->theme.length > 0 &&
        result->icons.length > 0 &&
        result->font.length > 0;
}

static inline void applyGTKDConfSettings(FFGTKResult* result, const char* themeName, const char* iconsName, const char* fontName)
{
    if(result->theme.length == 0)
        ffStrbufAppendS(&result->theme, themeName);

    if(result->icons.length == 0)
        ffStrbufAppendS(&result->icons, iconsName);

    if(result->font.length == 0)
        ffStrbufAppendS(&result->font, fontName);
}

static void detectGTKFromDConf(FFinstance* instance, FFGTKResult* result)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    static const char* themeName = NULL;
    static const char* iconsName = NULL;
    static const char* fontName = NULL;

    static bool init = false;

    pthread_mutex_lock(&mutex);

    if(init)
    {
        pthread_mutex_unlock(&mutex);
        applyGTKDConfSettings(result, themeName, iconsName, fontName);
        return;
    }

    init = true;

    const FFWMDEResult* wmde = ffDetectWMDE(instance);

    if(ffStrbufIgnCaseCompS(&wmde->dePrettyName, "Cinnamon") == 0)
    {
        themeName = ffSettingsGet(instance, "/org/cinnamon/desktop/interface/gtk-theme", "org.cinnamon.desktop.interface", NULL, "gtk-theme", FF_VARIANT_TYPE_STRING).strValue;
        iconsName = ffSettingsGet(instance, "/org/cinnamon/desktop/interface/icon-theme", "org.cinnamon.desktop.interface", NULL, "icon-theme", FF_VARIANT_TYPE_STRING).strValue;
        fontName = ffSettingsGet(instance, "/org/cinnamon/desktop/interface/font-name", "org.cinnamon.desktop.interface", NULL, "font-name", FF_VARIANT_TYPE_STRING).strValue;
    }
    else if(ffStrbufIgnCaseCompS(&wmde->dePrettyName, "Mate") == 0)
    {
        themeName = ffSettingsGet(instance, "/org/mate/interface/gtk-theme", "org.mate.interface", NULL, "gtk-theme", FF_VARIANT_TYPE_STRING).strValue;
        iconsName = ffSettingsGet(instance, "/org/mate/interface/icon-theme", "org.mate.interface", NULL, "icon-theme", FF_VARIANT_TYPE_STRING).strValue;
        fontName = ffSettingsGet(instance, "/org/mate/interface/font-name", "org.mate.interface", NULL, "font-name", FF_VARIANT_TYPE_STRING).strValue;
    }

    //Fallback + Gnome impl
    if(themeName == NULL)
        themeName = ffSettingsGet(instance, "/org/gnome/desktop/interface/gtk-theme", "org.gnome.desktop.interface", NULL, "gtk-theme", FF_VARIANT_TYPE_STRING).strValue;
    if(iconsName == NULL)
        iconsName = ffSettingsGet(instance, "/org/gnome/desktop/interface/icon-theme", "org.gnome.desktop.interface", NULL, "icon-theme", FF_VARIANT_TYPE_STRING).strValue;
    if(fontName == NULL)
        fontName = ffSettingsGet(instance, "/org/gnome/desktop/interface/font-name", "org.gnome.desktop.interface", NULL, "font-name", FF_VARIANT_TYPE_STRING).strValue;

    pthread_mutex_unlock(&mutex);
    applyGTKDConfSettings(result, themeName, iconsName, fontName);
}

static void detectGTKFromConfigFile(const char* filename, FFGTKResult* result)
{
    FILE* file = fopen(filename, "r");
    if(file == NULL)
        return;

    char* line = NULL;
    size_t len = 0;

    while(getline(&line, &len, file) != -1)
    {
        if(result->theme.length == 0)
            ffGetPropValue(line, "gtk-theme-name =", &result->theme);
        if(result->icons.length == 0)
            ffGetPropValue(line, "gtk-icon-theme-name =", &result->icons);
        if(result->font.length == 0)
            ffGetPropValue(line, "gtk-font-name =", &result->font);
    }

    if(line != NULL)
        free(line);

    fclose(file);
}

static void detectGTKFromConfigDir(FFstrbuf* configDir, const char* version, FFGTKResult* result)
{
    //In case of an empty env variable
    ffStrbufTrim(configDir, ' ');
    if(configDir->length == 0)
        return;

    uint32_t configDirLength = configDir->length;

    // <configdir>/gtk-<version>.0/settings.ini
    ffStrbufAppendS(configDir, "/gtk-");
    ffStrbufAppendS(configDir, version);
    ffStrbufAppendS(configDir, ".0/settings.ini");
    detectGTKFromConfigFile(configDir->chars, result);
    ffStrbufSubstrBefore(configDir, configDirLength);
    if(allPropertiesSet(result))
        return;

    // <configdir>/gtk-<version>.0/gtkrc
    ffStrbufAppendS(configDir, "/gtk-");
    ffStrbufAppendS(configDir, version);
    ffStrbufAppendS(configDir, ".0/gtkrc");
    detectGTKFromConfigFile(configDir->chars, result);
    ffStrbufSubstrBefore(configDir, configDirLength);
    if(allPropertiesSet(result))
        return;

    // <configdir>/gtkrc-<version>.0
    ffStrbufAppendS(configDir, "/gtkrc-");
    ffStrbufAppendS(configDir, version);
    ffStrbufAppendS(configDir, ".0");
    detectGTKFromConfigFile(configDir->chars, result);
    ffStrbufSubstrBefore(configDir, configDirLength);
    if(allPropertiesSet(result))
        return;

    // <configdir>/.gtkrc-<version>.0
    ffStrbufAppendS(configDir, "/.gtkrc-");
    ffStrbufAppendS(configDir, version);
    ffStrbufAppendS(configDir, ".0");
    detectGTKFromConfigFile(configDir->chars, result);
    ffStrbufSubstrBefore(configDir, configDirLength);
}

static void detectGTK(FFinstance* instance, const char* version, const char* envVariable, FFGTKResult* result)
{
    FFstrbuf buffer;
    ffStrbufInitA(&buffer, 128);

    uint32_t lastIndex;

    // From ENV: GTK*_RC_FILES

    ffStrbufSetS(&buffer, getenv(envVariable));
    lastIndex = 0;
    while (lastIndex < buffer.length)
    {
        uint32_t colonIndex = ffStrbufFirstIndexAfterC(&buffer, lastIndex, ':');
        buffer.chars[colonIndex] = '\0';

        detectGTKFromConfigFile(buffer.chars + lastIndex, result);
        if(allPropertiesSet(result))
        {
            ffStrbufDestroy(&buffer);
            return;
        }

        lastIndex = colonIndex + 1;
    }

    //From DConf / GSettings

    detectGTKFromDConf(instance, result);
    if(allPropertiesSet(result))
    {
        ffStrbufDestroy(&buffer);
        return;
    }

    //From config dirs

    //We need to do this because we use multiple threads on configDirs
    FFstrbuf baseDirCopy;
    ffStrbufInitA(&baseDirCopy, 64);

    for(uint32_t i = 0; i < instance->state.configDirs.length; i++)
    {
        FFstrbuf* baseDir = (FFstrbuf*) ffListGet(&instance->state.configDirs, i);
        ffStrbufSet(&baseDirCopy, baseDir);
        detectGTKFromConfigDir(&baseDirCopy, version, result);
        if(allPropertiesSet(result))
            break;
    }

    ffStrbufDestroy(&baseDirCopy);
    ffStrbufDestroy(&buffer);
}

#define FF_CALCULATE_GTK_IMPL(version) \
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; \
    static FFGTKResult result; \
    static bool init = false; \
    pthread_mutex_lock(&mutex); \
    if(init){ \
        pthread_mutex_unlock(&mutex);\
        return &result; \
    } \
    init = true; \
    ffStrbufInit(&result.theme); \
    ffStrbufInit(&result.icons); \
    ffStrbufInit(&result.font); \
    detectGTK(instance, #version, "GTK"#version"_RC_FILES", &result); \
    pthread_mutex_unlock(&mutex); \
    return &result;

const FFGTKResult* ffDetectGTK2(FFinstance* instance)
{
    FF_CALCULATE_GTK_IMPL(2)
}

const FFGTKResult* ffDetectGTK3(FFinstance* instance)
{
    FF_CALCULATE_GTK_IMPL(3)
}

const FFGTKResult* ffDetectGTK4(FFinstance* instance)
{
    FF_CALCULATE_GTK_IMPL(4)
}

#undef FF_CALCULATE_GTK_IMPL
