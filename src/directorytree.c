#include "directorytree.h"

static int lastUsedId = 0;

typedef void (*TimeoutCallback)(void);

FileSystemEntry *createEntry(const char *name, int isDirectory, FileSystemEntry *parent)
{
        FileSystemEntry *newEntry = (FileSystemEntry *)malloc(sizeof(FileSystemEntry));
        if (newEntry != NULL)
        {
                newEntry->name = strdup(name);

                newEntry->isDirectory = isDirectory;
                newEntry->isEnqueued = 0;
                newEntry->parent = parent;
                newEntry->children = NULL;
                newEntry->next = NULL;
                newEntry->id = ++lastUsedId;
                if (parent != NULL)
                {
                        newEntry->parentId = parent->id;
                }
                else
                {
                        newEntry->parentId = -1;
                }
        }
        return newEntry;
}

void addChild(FileSystemEntry *parent, FileSystemEntry *child)
{
        if (parent != NULL)
        {
                child->next = parent->children;
                parent->children = child;
        }
}

void setFullPath(FileSystemEntry *entry, const char *parentPath, const char *entryName)
{
        if (entry == NULL || parentPath == NULL || entryName == NULL)
        {

                return;
        }

        size_t fullPathLength = strlen(parentPath) + strlen(entryName) + 2; // +2 for '/' and '\0'

        entry->fullPath = (char *)malloc(fullPathLength);
        if (entry->fullPath == NULL)
        {
                return;
        }
        snprintf(entry->fullPath, fullPathLength, "%s/%s", parentPath, entryName);
}

void displayTreeSimple(FileSystemEntry *root, int depth)
{
        for (int i = 0; i < depth; ++i)
        {
                printf("  ");
        }

        printf("%s", root->name);
        if (root->isDirectory)
        {
                printf(" (Directory)\n");
                FileSystemEntry *child = root->children;
                while (child != NULL)
                {
                        displayTreeSimple(child, depth + 1);
                        child = child->next;
                }
        }
        else
        {
                printf(" (File)\n");
        }
}

void freeTree(FileSystemEntry *root)
{
        if (root == NULL)
        {
                return;
        }

        FileSystemEntry *child = root->children;
        while (child != NULL)
        {
                FileSystemEntry *next = child->next;
                freeTree(child);
                child = next;
        }

        free(root->name);
        free(root->fullPath);

        free(root);
}

int removeEmptyDirectories(FileSystemEntry *node)
{
        if (node == NULL)
        {
                return 0;
        }

        FileSystemEntry *currentChild = node->children;
        FileSystemEntry *prevChild = NULL;
        int numEntries = 0;

        while (currentChild != NULL)
        {
                if (currentChild->isDirectory)
                {
                        numEntries += removeEmptyDirectories(currentChild);

                        if (currentChild->children == NULL)
                        {
                                if (prevChild == NULL)
                                {
                                        node->children = currentChild->next;
                                }
                                else
                                {
                                        prevChild->next = currentChild->next;
                                }

                                FileSystemEntry *toFree = currentChild;
                                currentChild = currentChild->next;

                                free(toFree->name);
                                free(toFree->fullPath);
                                free(toFree);
                                numEntries++;
                                continue;
                        }
                }

                prevChild = currentChild;
                currentChild = currentChild->next;
        }
        return numEntries;
}

char *stringToUpperWithoutSpaces(const char *str)
{
        if (str == NULL)
        {
                return NULL;
        }

        size_t len = strlen(str);
        char *result = (char *)malloc(len + 1);
        if (result == NULL)
        {
                return NULL;
        }

        size_t resultIndex = 0;
        for (size_t i = 0; i < len; ++i)
        {
                if (!isspace((unsigned char)str[i]))
                {
                        result[resultIndex++] = toupper((unsigned char)str[i]);
                }
        }

        result[resultIndex] = '\0';

        return result;
}

int compareLibEntries(const struct dirent **a, const struct dirent **b)
{
        char *nameA = stringToUpperWithoutSpaces((*a)->d_name);
        char *nameB = stringToUpperWithoutSpaces((*b)->d_name);

        if (nameA[0] == '_' && nameB[0] != '_')
        {
                free(nameA);
                free(nameB);
                return 1;
        }
        else if (nameA[0] != '_' && nameB[0] == '_')
        {
                free(nameA);
                free(nameB);
                return -1;
        }

        int result = strcmp(nameB, nameA);
        free(nameA);
        free(nameB);

        return result;
}

int readDirectory(const char *path, FileSystemEntry *parent)
{

        DIR *directory = opendir(path);
        if (directory == NULL)
        {
                perror("Error opening directory");
                return 0;
        }

        struct dirent **entries;
        int dirEntries = scandir(path, &entries, NULL, compareLibEntries);
        if (dirEntries < 0)
        {
                perror("Error scanning directory entries");
                closedir(directory);
                return 0;
        }

        regex_t regex;
        regcomp(&regex, AUDIO_EXTENSIONS, REG_EXTENDED);

        int numEntries = 0;

        for (int i = 0; i < dirEntries; ++i)
        {
                struct dirent *entry = entries[i];

                if (entry->d_name[0] != '.' && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                {
                        char childPath[MAXPATHLEN];
                        snprintf(childPath, sizeof(childPath), "%s/%s", path, entry->d_name);

                        struct stat fileStats;
                        if (stat(childPath, &fileStats) == -1)
                        {
                                continue;
                        }

                        int isDirectory = true;

                        if (S_ISREG(fileStats.st_mode))
                        {
                                isDirectory = false;
                        }

                        char exto[6];
                        extractExtension(entry->d_name, sizeof(exto) - 1, exto);

                        int isAudio = match_regex(&regex, exto);

                        if (isAudio == 0 || isDirectory)
                        {
                                FileSystemEntry *child = createEntry(entry->d_name, isDirectory, parent);

                                if (entry != NULL)
                                {
                                        setFullPath(child, path, entry->d_name);
                                }

                                addChild(parent, child);

                                if (isDirectory)
                                {
                                        numEntries++;
                                        numEntries += readDirectory(childPath, child);
                                }
                        }
                }

                free(entry);
        }

        free(entries);
        regfree(&regex);

        closedir(directory);

        return numEntries;
}

void writeTreeToFile(FileSystemEntry *node, FILE *file, int parentId)
{
        if (node == NULL)
        {
                return;
        }

        fprintf(file, "%d\t%s\t%d\t%d\n", node->id, node->name, node->isDirectory, parentId);

        FileSystemEntry *child = node->children;
        FileSystemEntry *tmp = NULL;
        while (child)
        {
                tmp = child->next;
                writeTreeToFile(child, file, node->id);
                child = tmp;
        }

        free(node->name);
        free(node->fullPath);
        free(node);
}

void freeAndWriteTree(FileSystemEntry *root, const char *filename)
{
        FILE *file = fopen(filename, "w");
        if (!file)
        {
                perror("Failed to open file");
                return;
        }

        writeTreeToFile(root, file, -1);
        fclose(file);
}

FileSystemEntry *createDirectoryTree(const char *startPath, int *numEntries)
{
        FileSystemEntry *root = createEntry("root", 1, NULL);

        setFullPath(root, "", "");

        *numEntries = readDirectory(startPath, root);
        *numEntries -= removeEmptyDirectories(root);

        lastUsedId = 0;

        return root;
}

FileSystemEntry **resizeNodesArray(FileSystemEntry **nodes, int oldSize, int newSize)
{
        FileSystemEntry **newNodes = realloc(nodes, newSize * sizeof(FileSystemEntry *));
        if (newNodes)
        {                
                for (int i = oldSize; i < newSize; i++)
                {
                        newNodes[i] = NULL;
                }
        }
        return newNodes;
}

FileSystemEntry *reconstructTreeFromFile(const char *filename, const char *startMusicPath, int *numDirectoryEntries)
{
        FILE *file = fopen(filename, "r");
        if (!file)
        {
                return NULL;
        }

        char line[1024];
        int nodesCount = 0, nodesCapacity = 1000, oldCapacity = 0;
        FileSystemEntry **nodes = calloc(nodesCapacity, sizeof(FileSystemEntry *));
        if (!nodes)
        {
                fclose(file);
                return NULL;
        }

        FileSystemEntry *root = NULL;

        while (fgets(line, sizeof(line), file))
        {
                int id, parentId, isDirectory;
                char name[256];

                if (sscanf(line, "%d\t%255[^\t]\t%d\t%d", &id, name, &isDirectory, &parentId) == 4)
                {
                        if (id >= nodesCapacity)
                        {
                                oldCapacity = nodesCapacity;
                                nodesCapacity = id + 100;
                                FileSystemEntry **tempNodes = resizeNodesArray(nodes, oldCapacity, nodesCapacity);
                                if (!tempNodes)
                                {
                                        perror("Failed to resize nodes array");

                                        for (int i = 0; i < nodesCount; i++)
                                        {
                                                if (nodes[i])
                                                {
                                                        free(nodes[i]->name);
                                                        free(nodes[i]->fullPath);
                                                        free(nodes[i]);
                                                }
                                        }
                                        free(nodes);
                                        fclose(file);
                                        exit(EXIT_FAILURE);
                                }
                                nodes = tempNodes;
                        }

                        FileSystemEntry *node = malloc(sizeof(FileSystemEntry));
                        if (!node)
                        {
                                perror("Failed to allocate node");

                                fclose(file);
                                exit(EXIT_FAILURE);
                        }
                        node->id = id;
                        node->name = strdup(name);
                        node->isDirectory = isDirectory;
                        node->isEnqueued = 0;
                        node->children = node->next = node->parent = NULL;
                        nodes[id] = node;
                        nodesCount++;

                        if (parentId >= 0 && nodes[parentId])
                        {
                                node->parent = nodes[parentId];
                                if (nodes[parentId]->children)
                                {
                                        FileSystemEntry *child = nodes[parentId]->children;
                                        while (child->next)
                                        {
                                                child = child->next;
                                        }
                                        child->next = node;
                                }
                                else
                                {
                                        nodes[parentId]->children = node;
                                }

                                setFullPath(node, nodes[parentId]->fullPath, node->name);

                                if (isDirectory)
                                        *numDirectoryEntries = *numDirectoryEntries + 1;
                        }
                        else
                        {
                                root = node;
                                setFullPath(node, startMusicPath, "");
                        }
                }
        }
        fclose(file);
        free(nodes);

        return root;
}
