#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/appsupport.h"
#include "include/themes.h"
#include "include/system.h"
#include "include/ioman.h"
#include "include/util.h"

#include "include/usbsupport.h"
#include "include/ethsupport.h"
#include "include/hddsupport.h"
//START of OPL_DB tweaks
#include "include/supportbase.h"
//END of OPL_DB tweaks

static int appForceUpdate = 1;
static int appItemCount = 0;

static config_set_t *configApps;
static app_info_t *appsList;

struct app_info_linked {
    struct app_info_linked *next;
    app_info_t app;
};

// forward declaration
static item_list_t appItemList;

static void appFreeList(void);

static struct config_value_t *appGetConfigValue(int id)
{
    struct config_value_t *cur = configApps->head;

    while (id--) {
        cur = cur->next;
    }

    return cur;
}

static char *appGetELFName(char *name)
{
    // Looking for the ELF name
    char *pos = strrchr(name, '/');
    if (!pos)
        pos = strrchr(name, ':');
    if (pos) {
        return pos + 1;
    }

    return name;
}

static char *appGetBoot(char *device, int max, char *path)
{
    char *pos, *filenamesep;
    int len;

    // Looking for the boot device & filename from the path
    pos = strrchr(path, ':');
    if (pos != NULL) {
        len = (int)(pos + 1 - path);
        if (len + 1 > max)
            len = max - 1;
        strncpy(device, path, len);
        device[len] = '\0';
    }

    filenamesep = strchr(path, '/');
    if (filenamesep != NULL)
        return filenamesep + 1; 

    if (pos) {
        return pos + 1;
    }

    return path;
}

void appInit(void)
{
    LOG("APPSUPPORT Init\n");
    appForceUpdate = 1;
    configGetInt(configGetByType(CONFIG_OPL), "app_frames_delay", &appItemList.delay);
    configApps = configGetByType(CONFIG_APPS);
    appsList = NULL;
    appItemList.enabled = 1;
}

item_list_t *appGetObject(int initOnly)
{
    if (initOnly && !appItemList.enabled)
        return NULL;
    return &appItemList;
}

static int appNeedsUpdate(void)
{   // Always allow the user & auto refresh to refresh the apps list.
    return 1;
}

static int addAppsLegacyList(struct app_info_linked **appsLinkedList)
{
    struct config_value_t *cur;
    struct app_info_linked *app;
    int count;
    //START of OPL_DB tweaks
    char path[256];
	static item_list_t *listSupport = NULL;
	int ret=0; //Return from configRead
	
    configClear(configApps);

	//Try MC?:/OPL/conf_apps.cfg
	snprintf(path, sizeof(path), "%s/conf_apps.cfg", gBaseMCDir);
	configApps = configAlloc(CONFIG_APPS, NULL, path);
	ret = configRead(configApps);

	//Try HDD
	if ( ret == 0 && (listSupport = hddGetObject(1)) ) {
		if (configApps != NULL){
			configFree(configApps);
		}
		snprintf(path, sizeof(path), "%sconf_apps.cfg", hddGetPrefix());
		configApps = configAlloc(CONFIG_APPS, NULL, path);
		ret = configRead(configApps);
	}

	//Try ETH
	if ( ret == 0 && (listSupport = ethGetObject(1)) ) {
		if (configApps != NULL){
			configFree(configApps);
		}
		snprintf(path, sizeof(path), "%sconf_apps.cfg", ethGetPrefix());
		configApps = configAlloc(CONFIG_APPS, NULL, path);
		ret = configRead(configApps);	
	}

	//Try USB
	if ( ret == 0 && (listSupport = usbGetObject(1)) ){
		if (configApps != NULL){
			configFree(configApps);
		}
		snprintf(path, sizeof(path), "%sconf_apps.cfg", usbGetPrefix());
		configApps = configAlloc(CONFIG_APPS, NULL, path);
		ret = configRead(configApps);
	}
	//END of OPL_DB tweaks

    count = 0;
    cur = configApps->head;
    while (cur != NULL)
    {
        if (*appsLinkedList == NULL)
        {
            *appsLinkedList = malloc(sizeof(struct app_info_linked));
            app = *appsLinkedList;
            app->next = NULL;
        }
        else
        {
            app = malloc(sizeof(struct app_info_linked));
            if (app != NULL) {
              app->next = *appsLinkedList;
              *appsLinkedList = app;
            }
        }

        if (app == NULL)
        {
            LOG("APPSUPPORT unable to allocate memory.\n");
            break;
        }

        strncpy(app->app.title, cur->key, APP_TITLE_MAX + 1);
        app->app.title[APP_TITLE_MAX] = '\0';

        //Split the boot filename from the path.
        const char *elfname = appGetELFName(cur->val);
        if (elfname != cur->val) {
            strncpy(app->app.boot, elfname, APP_BOOT_MAX + 1);
            app->app.boot[APP_BOOT_MAX] = '\0';

            int pathlen = (int)(elfname - cur->val) - 1;
            if (cur->val[pathlen] == ':') //Discard only '/'.
                pathlen++;
            if (pathlen > APP_PATH_MAX)
                pathlen = APP_PATH_MAX;
            strncpy(app->app.path, cur->val, pathlen);
            app->app.path[pathlen] = '\0';
        } else {
            //Cannot split boot filename from the path, somehow.
            strncpy(app->app.boot, cur->val, APP_BOOT_MAX + 1);
            app->app.boot[APP_BOOT_MAX] = '\0';
            strncpy(app->app.path, cur->val, APP_PATH_MAX + 1);
            app->app.path[APP_BOOT_MAX] = '\0';
        }

        app->app.legacy = 1;
        count++;
        cur = cur->next;
    }

    return count;
}

static int appScanCallback(const char *path, config_set_t *appConfig, void *arg)
{
    struct app_info_linked **appsLinkedList = (struct app_info_linked **)arg;
    struct app_info_linked *app;
    const char *title, *boot;

    if (configGetStr(appConfig, APP_CONFIG_TITLE, &title) != 0
       && configGetStr(appConfig, APP_CONFIG_BOOT, &boot) != 0)
    {
        if (*appsLinkedList == NULL)
        {
            *appsLinkedList = malloc(sizeof(struct app_info_linked));
            app = *appsLinkedList;
            app->next = NULL;
        }
        else
        {
            app = malloc(sizeof(struct app_info_linked));
            if (app != NULL)
            {
              app->next = *appsLinkedList;
              *appsLinkedList = app;
            }
        }

        if (app == NULL)
        {
            LOG("APPSUPPORT unable to allocate memory.\n");
            return -1;
        }

        strncpy(app->app.title, title, APP_TITLE_MAX+1);
        app->app.title[APP_TITLE_MAX] = '\0';
        strncpy(app->app.boot, boot, APP_BOOT_MAX+1);
        app->app.boot[APP_BOOT_MAX] = '\0';
        strncpy(app->app.path, path, APP_PATH_MAX+1);
        app->app.path[APP_PATH_MAX] = '\0';
        app->app.legacy = 0;
        return 0;
    } else {
        LOG("APPSUPPORT item has no boot/title.\n");
        return 1;
    }

    return -1;
}

static int appUpdateItemList(void)
{
    struct app_info_linked *appsLinkedList, *appNext;
    int i;

    appFreeList();

    appsLinkedList = NULL;

    //Get legacy apps list first, so it is possible to use appGetConfigValue(id).
    appItemCount += addAppsLegacyList(&appsLinkedList);

    //Scan devices for apps.
    appItemCount += oplScanApps(&appScanCallback, &appsLinkedList);

    // Generate apps list
    if (appItemCount > 0)
    {
        appsList = malloc(appItemCount * sizeof(app_info_t));

        if (appsList != NULL)
        {
            for (i = 0; appsLinkedList != NULL; i++)
            {   //appsLinkedList contains items in reverse order.
                memcpy(&appsList[appItemCount - i - 1], &appsLinkedList->app, sizeof(app_info_t));

                appNext = appsLinkedList->next;
                free(appsLinkedList);
                appsLinkedList = appNext;
            }
        }
        else
        {
            LOG("APPSUPPORT unable to allocate memory.\n");
            appItemCount = 0;
        }
    }

    LOG("APPSUPPORT %d apps loaded\n", appItemCount);

    return appItemCount;
}

static void appFreeList(void)
{
    if (appsList != NULL)
    {
        appsList = NULL;
        appItemCount = 0;
    }
}

static int appGetItemCount(void)
{
    return appItemCount;
}

static char *appGetItemName(int id)
{
    return appsList[id].title;
}

static int appGetItemNameLength(int id)
{
    return CONFIG_KEY_NAME_LEN;
}

/* appGetItemStartup() is called to get the startup path for display & for the art assets.
   The path is used immediately, before a subsequent call to appGetItemStartup(). */
static char *appGetItemStartup(int id)
{
    if (appsList[id].legacy)
    {
        struct config_value_t *cur = appGetConfigValue(id);
        return appGetELFName(cur->val);
    } else {
        return appsList[id].boot;
    }
}

static void appDeleteItem(int id)
{
    if (appsList[id].legacy)
    {
        struct config_value_t *cur = appGetConfigValue(id);
        fileXioRemove(cur->val);
        cur->key[0] = '\0';
        configApps->modified = 1;
        configWrite(configApps);
    } else {
        sysDeleteFolder(appsList[id].path);
    }

    appForceUpdate = 1;
}

static void appRenameItem(int id, char *newName)
{
    char value[256];

    if (appsList[id].legacy)
    {
        struct config_value_t *cur = appGetConfigValue(id);

        strncpy(value, cur->val, sizeof(value));
        configRemoveKey(configApps, cur->key);
        configSetStr(configApps, newName, value);
        configWrite(configApps);
    } else {
        config_set_t *appConfig;

        snprintf(value, sizeof(value), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

        appConfig = configAlloc(0, NULL, value);
        if (appConfig != NULL)
        {
            configRead(appConfig);
            configSetStr(appConfig, APP_CONFIG_TITLE, newName);
            configWrite(appConfig);

            configFree(appConfig);
       }
    }

    appForceUpdate = 1;
}

static void appLaunchItem(int id, config_set_t *configSet)
{
    int mode, fd;
    const char *filename;

    //Retrieve configuration set by appGetConfig()
    configGetStr(configSet, CONFIG_ITEM_STARTUP, &filename);

    fd = fileXioOpen(filename, O_RDONLY);
    if (fd >= 0) {
        fileXioClose(fd);

        //To keep the necessary device accessible, we will assume the mode that owns the device which contains the file to boot.
        mode = oplPath2Mode(filename);
        if (mode < 0)
            mode = APP_MODE; //Legacy apps mode (mc?:/*)

        deinit(UNMOUNT_EXCEPTION, mode); // CAREFUL: deinit will call appCleanUp, so configApps/cur will be freed
        sysExecElf(filename);
    } else
        guiMsgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
}

static config_set_t *appGetConfig(int id)
{
    config_set_t *config = NULL;

    if (appsList[id].legacy) {
        config = configAlloc(0, NULL, NULL);
        struct config_value_t *cur = appGetConfigValue(id);
        //START of OPL_DB tweaks
        static item_list_t *listSupport = NULL;
        int ret=0;

        //Search on HDD, SMB, USB for the CFG/GAME.ELF.CFG file.
        //HDD
        if ( (listSupport = hddGetObject(1)) ) {
            char path[256];
            #if OPL_IS_DEV_BUILD
                snprintf(path, sizeof(path), "%sCFG-DEV/%s.cfg", hddGetPrefix(), appGetELFName(cur->val));
            #else
                snprintf(path, sizeof(path), "%sCFG/%s.cfg", hddGetPrefix(), appGetELFName(cur->val));
            #endif
            config = configAlloc(0, NULL, path);
            ret = configRead(config);
        }

        //ETH
        if ( ret == 0 && (listSupport = ethGetObject(1)) ) {
            char path[256];
            if (config != NULL)
                configFree(config);
            
            #if OPL_IS_DEV_BUILD
                snprintf(path, sizeof(path), "%sCFG-DEV/%s.cfg", ethGetPrefix(), appGetELFName(cur->val));
            #else
                snprintf(path, sizeof(path), "%sCFG/%s.cfg", ethGetPrefix(),appGetELFName(cur->val));
            #endif
            config = configAlloc(0, NULL, path);
            ret = configRead(config);	
        }

        //USB
        if ( ret == 0 && (listSupport = usbGetObject(1)) ){
            char path[256];
            if (config != NULL)
                configFree(config);
            
            #if OPL_IS_DEV_BUILD
                snprintf(path, sizeof(path), "%sCFG-DEV/%s.cfg", usbGetPrefix(),  appGetELFName(cur->val));
            #else
                snprintf(path, sizeof(path), "%sCFG/%s.cfg", usbGetPrefix(), appGetELFName(cur->val));
            #endif
            config = configAlloc(0, NULL, path);
            ret = configRead(config);
        }

        if (ret == 0){ //No config found on previous devices, create one.
            if (config != NULL)
                configFree(config);
            
            config = configAlloc(0, NULL, NULL);
        }
        //END of OPL_DB tweaks
        configSetStr(config, CONFIG_ITEM_NAME, appGetELFName(cur->val));
        configSetStr(config, CONFIG_ITEM_LONGNAME, cur->key);
        configSetStr(config, CONFIG_ITEM_STARTUP, cur->val);
        //START of OPL_DB tweaks
        configSetStr(config, CONFIG_ITEM_FORMAT, "ELF");
	    configSetStr(config, CONFIG_ITEM_MEDIA, "PS2");
        //END of OPL_DB tweaks
    } else {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

        config = configAlloc(0, NULL, path);
        configRead(config);  //Does not matter if the config file could be loaded or not.

        configSetStr(config, CONFIG_ITEM_NAME, appsList[id].boot);
        configSetStr(config, CONFIG_ITEM_LONGNAME, appsList[id].title);
        snprintf(path, sizeof(path), "%s/%s", appsList[id].path, appsList[id].boot);
        configSetStr(config, CONFIG_ITEM_STARTUP, path);
        //START of OPL_DB tweaks
        configSetStr(config, CONFIG_ITEM_FORMAT, "ELF");
	    configSetStr(config, CONFIG_ITEM_MEDIA, "PS2");
        //END of OPL_DB tweaks
    }
    return config;
}

static int appGetImage(char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char device[8], *startup;

    startup = appGetBoot(device, sizeof(device), value);

    // Try with entire value, fixes not loading theme icons in subfolders.
    if (oplGetAppImage(device, folder, isRelative, value, suffix, resultTex, psm) >=0){
        return 0;
    }

    return oplGetAppImage(device, folder, isRelative, startup, suffix, resultTex, psm);
}

//This may be called, even if appInit() was not.
static void appCleanUp(int exception)
{
    if (appItemList.enabled) {
        LOG("APPSUPPORT CleanUp\n");

        appFreeList();
    }
}

//This may be called, even if appInit() was not.
static void appShutdown(void)
{
    if (appItemList.enabled) {
        LOG("APPSUPPORT Shutdown\n");

        appFreeList();
    }
}

static item_list_t appItemList = {
    APP_MODE, -1, 0, MODE_FLAG_NO_COMPAT | MODE_FLAG_NO_UPDATE, MENU_MIN_INACTIVE_FRAMES, APP_MODE_UPDATE_DELAY, "Applications", _STR_APPS, NULL, &appInit, &appNeedsUpdate, &appUpdateItemList,
    &appGetItemCount, NULL, &appGetItemName, &appGetItemNameLength, &appGetItemStartup, &appDeleteItem, &appRenameItem, &appLaunchItem,
    &appGetConfig, &appGetImage, &appCleanUp, &appShutdown, NULL, APP_ICON
};
