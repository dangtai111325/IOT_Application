#include "ai_engine.h"

#include <math.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#include "app_state.h"
#include "dht_anomaly_model.h"

namespace sender
{

namespace
{

tflite::MicroErrorReporter g_microErrorReporter;
tflite::ErrorReporter *g_errorReporter = nullptr;
const tflite::Model *g_model = nullptr;
tflite::MicroInterpreter *g_interpreter = nullptr;
TfLiteTensor *g_input = nullptr;
TfLiteTensor *g_output = nullptr;
tflite::AllOpsResolver g_resolver;
uint8_t g_tensorArena[AI_TENSOR_ARENA_SIZE];

} // namespace

bool setupAiEngine()
{
    g_errorReporter = &g_microErrorReporter;
    g_model = tflite::GetModel(dht_anomaly_model_tflite);
    if (g_model == nullptr)
    {
        return false;
    }

    static tflite::MicroInterpreter interpreter(
        g_model,
        g_resolver,
        g_tensorArena,
        AI_TENSOR_ARENA_SIZE,
        g_errorReporter);

    g_interpreter = &interpreter;
    if (g_interpreter->AllocateTensors() != kTfLiteOk)
    {
        return false;
    }

    g_input = g_interpreter->input(0);
    g_output = g_interpreter->output(0);
    return g_input != nullptr && g_output != nullptr;
}

void runAiInference()
{
    if (g_interpreter == nullptr || g_input == nullptr || g_output == nullptr)
    {
        g_aiScore = NAN;
        g_aiStatus = "AI ERROR";
        return;
    }

    g_input->data.f[0] = g_temperature;
    g_input->data.f[1] = g_humidity;

    if (g_interpreter->Invoke() != kTfLiteOk)
    {
        g_aiScore = NAN;
        g_aiStatus = "AI ERROR";
        return;
    }

    g_aiScore = g_output->data.f[0];
    if (!isfinite(g_aiScore))
    {
        g_aiScore = NAN;
        g_aiStatus = "AI ERROR";
        return;
    }

    if (g_aiScore < 0.0f)
    {
        g_aiScore = 0.0f;
    }
    else if (g_aiScore > 1.0f)
    {
        g_aiScore = 1.0f;
    }

    g_aiStatus = g_aiScore >= g_config.threshold ? "ANOMALY" : "NORMAL";
}

} // namespace sender
