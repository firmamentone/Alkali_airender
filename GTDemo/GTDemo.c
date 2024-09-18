#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>  // For getcwd()
#include <limits.h>  // For PATH_MAX

#define VERSION "0001"
#define MAX_COMMAND_LENGTH 2048

void displayMenu() {
    char rtspString[256] = "Not available";

    // Open and read the RTSP string from the file
    FILE *file = fopen("rtspstring.txt", "r");
    if (file != NULL) {
        if (fgets(rtspString, sizeof(rtspString), file) != NULL) {
            // Remove the newline character if present
            rtspString[strcspn(rtspString, "\n")] = 0;
        }
        fclose(file);
    }

    printf("Version %s\n", VERSION);
    printf("Current RTSP String: %s\n", rtspString);
    printf("Please select an option:\n");
    printf("1. Person RTSP\n");
    printf("2. Forklift RTSP\n");
    printf("3. Person_Forklift RTSP\n");
    printf("4. RTSP setting\n");
    printf("5. Person Test\n");
    printf("6. Forklift Test\n");
    printf("7. Person_Forklift Test\n");
}

void rtspSetting() {
    FILE *file;
    char buffer[256];

    // Read the file content
    file = fopen("rtspstring.txt", "r");
    if (file == NULL) {
        printf("Failed to open rtspstring.txt\n");
        return;
    }

    printf("Current RTSP string:\n");
    while (fgets(buffer, sizeof(buffer), file)) {
        printf("%s", buffer);
    }
    fclose(file);

    // Ask the user if they want to modify the content
    char choice;
    printf("\nDo you want to modify the RTSP string? (y/n): ");
    scanf(" %c", &choice);

    if (choice == 'y' || choice == 'Y') {
        printf("Enter new RTSP string: ");
        scanf(" %[^\n]", buffer);  // Read a line of input, including spaces

        // Write the new content to the file
        file = fopen("rtspstring.txt", "w");
        if (file == NULL) {
            printf("Failed to open rtspstring.txt for writing\n");
            return;
        }
        fprintf(file, "%s\n", buffer);
        fclose(file);
        printf("RTSP string has been updated.\n");
    } else {
        printf("RTSP string remains unchanged.\n");
    }
}

void modifyModelNameAndPathInJson(const char *filename, const char *new_model_name) {
    FILE *file;
    struct json_object *parsed_json;
    struct json_object *kernels_array;
    struct json_object *kernel_object;
    struct json_object *config_object;
    struct json_object *model_name_object;
    struct json_object *model_path_object;

    // Get the current working directory
    char current_path[PATH_MAX];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        printf("Failed to get current working directory.\n");
        return;
    }

    // Append "model/" to the current path to form the model path
    char model_path[PATH_MAX];
    snprintf(model_path, sizeof(model_path), "%s/model/", current_path);

    // Open and read the JSON file
    file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open %s\n", filename);
        return;
    }

    char buffer[2048];
    fread(buffer, sizeof(char), 2048, file);
    fclose(file);

    // Parse JSON
    parsed_json = json_tokener_parse(buffer);
    if (parsed_json == NULL) {
        printf("Error parsing JSON data.\n");
        return;
    }

    // Access the kernels array and then the config object
    if (json_object_object_get_ex(parsed_json, "kernels", &kernels_array)) {
        kernel_object = json_object_array_get_idx(kernels_array, 0);
        if (kernel_object != NULL && json_object_object_get_ex(kernel_object, "config", &config_object)) {
            // Modify the "model-name"
            if (json_object_object_get_ex(config_object, "model-name", &model_name_object)) {
                json_object_set_string(model_name_object, new_model_name);
            }

            // Modify the "model-path"
            if (json_object_object_get_ex(config_object, "model-path", &model_path_object)) {
                json_object_set_string(model_path_object, model_path);
            } else {
                // If "model-path" doesn't exist, add it
                json_object_object_add(config_object, "model-path", json_object_new_string(model_path));
            }
        }
    }

    // Write the modified JSON back to the file
    file = fopen(filename, "w");
    if (file == NULL) {
        printf("Failed to open %s for writing\n", filename);
        json_object_put(parsed_json); // Free memory
        return;
    }

    // Only write back the modified JSON, preserving other data
    fprintf(file, "%s", json_object_to_json_string_ext(parsed_json, JSON_C_TO_STRING_PRETTY));
    fclose(file);
    json_object_put(parsed_json); // Free memory

    printf("Model name and path in %s have been updated to %s and %s.\n", filename, new_model_name, model_path);
}
void executeGStreamerPipeline(const char *gstCommand) {
    int ret = system(gstCommand);
    if (ret == -1) {
        printf("Failed to execute GStreamer pipeline.\n");
    }
}

void startRTSPStream(const char *model_name) {
    // Get the current working directory
    char current_path[PATH_MAX];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        printf("Failed to get current working directory.\n");
        return;
    }

    // Modify the model name and model path in the JSON file
    modifyModelNameAndPathInJson("aiinference.json", model_name);
    // Read the RTSP string from the file
    char rtspString[256] = "";
    FILE *file = fopen("rtspstring.txt", "r");
    if (file != NULL) {
        if (fgets(rtspString, sizeof(rtspString), file) != NULL) {
            // Remove the newline character if present
            rtspString[strcspn(rtspString, "\n")] = 0;
        }
        fclose(file);
    } else {
        printf("Failed to open rtspstring.txt. Cannot proceed with GStreamer.\n");
        return;
    }

    // Construct the GStreamer pipeline command
    char gstCommand[MAX_COMMAND_LENGTH];
    snprintf(gstCommand, sizeof(gstCommand),
             "gst-launch-1.0 rtspsrc location=\"%s\" ! queue ! rtph264depay ! queue "
             "! h264parse ! queue ! omxh264dec "
             "! queue max-size-buffers=2 leaky=2 ! tee name=t0 t0.src_0 ! queue "
             "! ivas_xmultisrc kconfig=%s/preprocess.json ! queue "
             "! ivas_xfilter kernels-config=%s/aiinference.json "
             "! ima.sink_master ivas_xmetaaffixer name=ima ima.src_master "
             "! fakesink t0.src_1 ! queue max-size-buffers=1 leaky=2 "
             "! ima.sink_slave_0 ima.src_slave_0 ! queue "
             "! ivas_xfilter kernels-config=%s/drawresult.json ! queue "
             "! kmssink driver-name=xlnx plane-id=39 sync=false fullscreen-overlay=true",
             rtspString, current_path, current_path, current_path);

    // Execute the GStreamer command
    executeGStreamerPipeline(gstCommand);
}

void personRTSP() {
    startRTSPStream("tyv3_person");
}

void forkliftRTSP() {
    startRTSPStream("tyv3_stacker");
}

void personForkliftRTSP() {
    startRTSPStream("person_stacker");
}

void personTest() {
    // Get the current working directory
    char current_path[PATH_MAX];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        printf("Failed to get current working directory.\n");
        return;
    }

    // Modify the model name to "tyv3_person"
    modifyModelNameAndPathInJson("aiinference.json", "tyv3_person");

    // Construct the GStreamer pipeline command for Person Test
    char gstCommand[MAX_COMMAND_LENGTH];
    snprintf(gstCommand, sizeof(gstCommand),
             "gst-launch-1.0 multifilesrc location=\"demotest.h264\" "
             "! h264parse ! queue ! omxh264dec "
             "! queue ! tee name=t0 t0.src_0 ! queue "
             "! ivas_xmultisrc kconfig=%s/preprocess.json ! queue "
             "! ivas_xfilter kernels-config=%s/aiinference.json "
             "! ima.sink_master ivas_xmetaaffixer name=ima ima.src_master "
             "! fakesink t0.src_1 ! queue "
             "! ima.sink_slave_0 ima.src_slave_0 ! queue "
             "! ivas_xfilter kernels-config=%s/drawresult.json ! queue "
             "! kmssink driver-name=xlnx plane-id=39 sync=false fullscreen-overlay=true",
             current_path, current_path, current_path);

    // Execute the GStreamer command
    executeGStreamerPipeline(gstCommand);
}

void forkliftTest() {
    // Get the current working directory
    char current_path[PATH_MAX];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        printf("Failed to get current working directory.\n");
        return;
    }

    // Modify the model name to "tyv3_stacker"
    modifyModelNameAndPathInJson("aiinference.json", "tyv3_stacker");

    // Construct the GStreamer pipeline command for Forklift Test
    char gstCommand[MAX_COMMAND_LENGTH];
    snprintf(gstCommand, sizeof(gstCommand),
             "gst-launch-1.0 multifilesrc location=\"demotest.h264\" "
             "! h264parse ! queue ! omxh264dec "
             "! queue ! tee name=t0 t0.src_0 ! queue "
             "! ivas_xmultisrc kconfig=%s/preprocess.json ! queue "
             "! ivas_xfilter kernels-config=%s/aiinference.json "
             "! ima.sink_master ivas_xmetaaffixer name=ima ima.src_master "
             "! fakesink t0.src_1 ! queue "
             "! ima.sink_slave_0 ima.src_slave_0 ! queue "
             "! ivas_xfilter kernels-config=%s/drawresult.json ! queue "
             "! kmssink driver-name=xlnx plane-id=39 sync=false fullscreen-overlay=true",
             current_path, current_path, current_path);

    // Execute the GStreamer command
    executeGStreamerPipeline(gstCommand);
}

void personForkliftTest() {
    // Get the current working directory
    char current_path[PATH_MAX];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        printf("Failed to get current working directory.\n");
        return;
    }

    // Modify the model name to "person_stacker"
    modifyModelNameAndPathInJson("aiinference.json", "person_stacker");

    // Construct the GStreamer pipeline command for Person_Forklift Test
    char gstCommand[MAX_COMMAND_LENGTH];
    snprintf(gstCommand, sizeof(gstCommand),
             "gst-launch-1.0 multifilesrc location=\"demotest.h264\" "
             "! h264parse ! queue ! omxh264dec "
             "! queue ! tee name=t0 t0.src_0 ! queue "
             "! ivas_xmultisrc kconfig=%s/preprocess.json ! queue "
             "! ivas_xfilter kernels-config=%s/aiinference.json "
             "! ima.sink_master ivas_xmetaaffixer name=ima ima.src_master "
             "! fakesink t0.src_1 ! queue "
             "! ima.sink_slave_0 ima.src_slave_0 ! queue "
             "! ivas_xfilter kernels-config=%s/drawresult.json ! queue "
             "! kmssink driver-name=xlnx plane-id=39 sync=false fullscreen-overlay=true",
             current_path, current_path, current_path);

    // Execute the GStreamer command
    executeGStreamerPipeline(gstCommand);
}

int main() {
    int choice;
    char input[256];
    while (1) {
        displayMenu();
        printf("Enter your choice: ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Error reading input. Please try again.\n");
            continue;
        }

        // Remove newline character if present
        input[strcspn(input, "\n")] = 0;

        // Try to convert input to integer
        if (sscanf(input, "%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        switch (choice) {
            case 1:
                personRTSP();
                break;
            case 2:
                forkliftRTSP();
                break;
            case 3:
                personForkliftRTSP();
                break;
            case 4:
                rtspSetting();
                break;
            case 5:
                personTest();
                break;
            case 6:
                forkliftTest();
                break;
            case 7:
                personForkliftTest();
                break;
            default:
                printf("Invalid choice, please try again.\n");
                break;
        }
    }

    return 0;
}
