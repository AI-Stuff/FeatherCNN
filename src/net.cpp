//Tencent is pleased to support the open source community by making FeatherCNN available.

//Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.

//Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//in compliance with the License. You may obtain a copy of the License at
//
//https://opensource.org/licenses/BSD-3-Clause
//
//Unless required by applicable law or agreed to in writing, software distributed
//under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//CONDITIONS OF ANY KIND, either express or implied. See the License for the
//specific language governing permissions and limitations under the License.

#include "feather_generated.h"
#include "layer_factory.h"
#include "net.h"
#include "layer.h"
#include "layers/input_layer.h"
#include "mempool.h"

#include "booster/helper.h"

#include <stdio.h>
#include <cstring>
#define LAYER_TIMING
#define PRINT_SETUP_LOG

namespace feather
{
Net::Net(size_t num_threads)
{
    register_layer_creators();
    CommonMemPool<float> *mempool = new CommonMemPool<float>();
    rt_param = new RuntimeParameter<float>(mempool, num_threads);
}


Net::~Net()
{
    for(int i = 0; i < layers.size(); ++i)
    {
        delete layers[i];
    }
    delete rt_param->common_mempool();
    delete rt_param;
}

int Net::ExtractBlob(float* output_ptr, std::string name)
{
    if (blob_map.find(std::string(name)) == blob_map.end())
    {
        LOGE("Cannot find blob %s\n", name.c_str());
        return -1;
    }
    const Blob<float> *p_blob = blob_map[name];
    const size_t data_size = p_blob->data_size();
    const float *data = p_blob->data();

    memcpy(output_ptr, data, sizeof(float) * data_size);
    return 0;
}

int Net::PrintBlobData(std::string blob_name)
{
    size_t data_size;
    this->GetBlobDataSize(&data_size, blob_name);
    float *arr = (float*) malloc(sizeof(float) * data_size);
    this->ExtractBlob(arr, blob_name);
    size_t len = data_size;

    for (int i = 0; i < len; ++i)
    {
        LOGD("%f\t", arr[i]);
    }
    LOGD("\n");
    free(arr);
	
    return 0;
}

int Net::GetBlobDataSize(size_t *data_size, std::string name)
{
    if (blob_map.find(std::string(name)) == blob_map.end())
    {
        LOGE("Cannot find blob %s\n", name.c_str());
        return -1;
    }
    const Blob<float> *p_blob = blob_map[name];
    *data_size = p_blob->data_size();
    return 0;
}
#include <unistd.h>
int Net::Forward(float *input)
{
    InputLayer *input_layer = (InputLayer *)layers[0];
    for (int i = 0; i < input_layer->input_size(); ++i)
    {
        input_layer->CopyInput(input_layer->input_name(i), input);
    }
    
    for (int i = 1; i < layers.size(); ++i)
    {
        // sleep(2);
#ifdef LAYER_TIMING
        timespec tpstart, tpend;
        LOGD("Entering layer %s type %s\n", layers[i]->name().c_str(), layers[i]->type().c_str());
        clock_gettime(CLOCK_MONOTONIC, &tpstart);
#endif
        //LOGD("Forward layer%d:%s %s\n", i, layers[i]->name().c_str(), layers[i]->type().c_str());
        layers[i]->Forward();
#if 0
        for (size_t j = 0; j < layers[i]->top_blob_size(); j++)
            layers[i]->top_blob(j)->PrintBlobInfo();

	PrintBlobData(layers[i]->name());	
#endif
#ifdef LAYER_TIMING
        clock_gettime(CLOCK_MONOTONIC, &tpend);
        double timedif = 1000000.0 * (tpend.tv_sec - tpstart.tv_sec) + (tpend.tv_nsec - tpstart.tv_nsec) / 1000.0;
        LOGD("Layer %s type %s spent %lfms\n", layers[i]->name().c_str(), layers[i]->type().c_str(), timedif / 1000.0);
#endif
    }
    return 0;
}

int Net::Forward(float* input, int height, int width)
{
    InputLayer *input_layer = (InputLayer *)layers[0];
    input_layer->Reshape(input_layer->input_name(0), height, width);
    input_layer->CopyInput(input_layer->input_name(0), input);
    for (int i = 1; i < layers.size(); ++i)
    {
#ifdef LAYER_TIMING
        timespec tpstart, tpend;
        clock_gettime(CLOCK_MONOTONIC, &tpstart);
#endif
// LOGI("Forward layer%d:%s %s\n", i, layers[i]->name().c_str(), layers[i]->type().c_str());
        layers[i]->ForwardReshape();
#ifdef LAYER_TIMING
        clock_gettime(CLOCK_MONOTONIC, &tpend);
        double timedif = 1000000.0 * (tpend.tv_sec - tpstart.tv_sec) + (tpend.tv_nsec - tpstart.tv_nsec) / 1000.0;
        LOGI("layer %s type %s spent %lfms\n", layers[i]->name().c_str(), layers[i]->type().c_str(), timedif / 1000.0);
#endif
    }
    return 0;
}

void Net::TraverseNet()
{
    for (int i = 0; i < layers.size(); ++i)
    {
        LOGD("Layer %s %s %s\n", layers[i]->name().c_str(),
               layers[i]->bottom(0).c_str(),
               layers[i]->top(0).c_str());
    }
}

void Net::InitFromPath(const char *model_path)
{
    FILE *fp = NULL;
    fp = fopen(model_path, "rb");
    if (fp == NULL)
    {
        LOGE("Cannot open feather model!\n");
        exit(-1);
    }
    fseek ( fp , 0 , SEEK_SET );
    this->InitFromFile(fp);
    fclose(fp);
}


void Net::InitFromStringPath(std::string model_path)
{
    LOGI("Init model path %s", model_path.c_str());
	InitFromPath(model_path.c_str());
}

void Net::InitFromFile(FILE* fp)
{
    if (fp == NULL)
    {
        LOGE("Cannot open feather model!\n");
        exit(-1);
    }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *net_buffer = (uint8_t *) malloc(sizeof(uint8_t) * file_size);
    size_t read_size = fread(net_buffer, sizeof(uint8_t), file_size, fp);
    if (read_size != file_size)
    {
        LOGE("Reading model failed! file_size %ld read size %ld\n", file_size, read_size);
        exit(-1);
    }
    LOGD("Finished loading from file");
    this->InitFromBuffer(net_buffer);
    free(net_buffer);
}

int Net::RemoveLayer(Layer* target_layer)
{
	if(target_layer->bottom_size() != 1 || target_layer->top_size() != 1)
	{
		LOGE("Cannot remove target layer %s type %s with mutliple input/outputs!", target_layer->name(), target_layer->type());
		return -1;
	}

	std::string new_bottom = target_layer->bottom(0);
	std::string old_bottom = target_layer->top(0);
#ifdef PRINT_SETUP_LOG
                LOGD("Old bottom %s to new bottom %s", old_bottom.c_str(), new_bottom.c_str());
#endif
	const Blob<float> * new_bottom_blob = target_layer->bottom_blob(0);

	for(int i = 0; i < layers.size(); ++i)
	{
		if(layers[i] == target_layer)
		{
			layers.erase(layers.begin() + i);
			--i;
			continue;
		}
		Layer *next_layer = layers[i];
		for(int b = 0; b < next_layer->bottom_size(); ++b)
		{
			if (next_layer->bottom(b).compare(old_bottom) == 0)
			{
				next_layer->ReplaceBottomBlob(old_bottom, new_bottom, new_bottom_blob);
				break;
			}
		}
	}
	delete target_layer;
	return 0;
}

bool Net::InitFromBuffer(const void *net_buffer)
{
    //rt_param in the param list just to distinguish.
    const NetParameter *net_param = feather::GetNetParameter(net_buffer);
    size_t layer_num = VectorLength(net_param->layer());
    //Find input layer.
#ifdef PRINT_SETUP_LOG
    LOGD("Loading %d layers", layer_num);
#endif
    for (int i = 0; i < layer_num; ++i)
    {
        if (net_param->layer()->Get(i)->type()->str().compare("Input") == 0)
        {
            layers.push_back(LayerRegistry::CreateLayer(net_param->layer()->Get(i), rt_param));
            break;
        }
    }
    for (int i = 1; i < layer_num; ++i)
    {
        const LayerParameter *layer_param = net_param->layer()->Get(i);
        Layer *new_layer = LayerRegistry::CreateLayer(layer_param, rt_param);
#ifdef PRINT_SETUP_LOG
        LOGD("Setup layer %s\n", layer_param->name()->c_str());
#endif
        layers.push_back(new_layer);
    }
    
#ifdef PRINT_SETUP_LOG
        LOGD("Layer setup finish");
#endif
    //Generate top blobs, will check the dependency.
    for (int i = 0; i < layers.size(); ++i)
    {
        size_t top_num = layers[i]->top_size();
        size_t top_blob_num = layers[i]->top_blob_size();
        if (top_blob_num == 0)
        {
            for (int b = 0; b < layers[i]->bottom_size(); ++b)
            {
                std::string blob_name = layers[i]->bottom(b);
#ifdef PRINT_SETUP_LOG
                LOGI("Setting up blob %s", blob_name.c_str());
#endif
                //TODO handle error: when blob_name has not been inserted into map.
                if (blob_map.find(blob_name) != blob_map.end())
                    layers[i]->SetupBottomBlob(blob_map[blob_name], blob_name);
                else
                {
                    LOGE("Blob %s not setup yet, may be casued by wrong layer order. Aborted.\n");
                    exit(-1);
                }
            }
            layers[i]->GenerateTopBlobs();
        }
        for (int t = 0; t < top_num; ++t)
        {
            std::string blob_name = layers[i]->top(t);
            blob_map[blob_name] = layers[i]->top_blob(blob_name);
            //blob_map[blob_name]->PrintBlobInfo();
        }
    }

    //Try to fuse some layers together
    for (int i = 1; i < layers.size() - 1; ++i)
    {
        if (!layers[i]->fusible())
            continue;
        for (int j = i + 1; j < layers.size(); ++j)
        {
            Layer *next_layer = layers[j];
            while (layers[i]->TryFuse(next_layer) == 1)
            {
#if 0
                //Update the respective bottoms in other layers.
                std::string new_bottom = layers[i]->top(0);
                std::string old_bottom = next_layer->top(0);
#ifdef PRINT_SETUP_LOG
                LOGD("Old bottom %s to new bottom %s\n", old_bottom.c_str(), new_bottom.c_str());
#endif
                for (int k = i + 1; k < layers.size(); ++k)
                {
                    if (k == j)
                        continue;

                    for (int b = 0; b < layers[k]->bottom_size(); ++b)
                    {
                        if (layers[k]->bottom(b).compare(old_bottom) == 0)
                            layers[k]->ReplaceBottomBlob(old_bottom, new_bottom, layers[i]->top_blob(0));
                    }
                }
		delete layers[j];
                layers.erase(layers.begin() + j);
#else
		this->RemoveLayer(layers[j]);
#endif
#ifdef PRINT_SETUP_LOG
                LOGD("Erased layer %d %s\n", j, next_layer->name().c_str());
#endif
                next_layer = layers[j];
#ifdef PRINT_SETUP_LOG
                LOGD("Layer %d after erasing: %s type %s\n", j, next_layer->name().c_str(), next_layer->type().c_str());
#endif
            }
        }
    }
    //Remove Dropout Layers
    for (int i = 0; i < layers.size() - 1; ++i)
    {
	    if(layers[i]->type().compare("Dropout") == 0)
	    {
		  
#ifdef PRINT_SETUP_LOG
                LOGD("Erase layer %d %s\n", i, layers[i]->name().c_str(), layers[i]->type().c_str());
#endif
		this->RemoveLayer(layers[i]);
#ifdef PRINT_SETUP_LOG
                LOGD("Layer %d after erasing: %s type %s\n", i, layers[i]->name().c_str(), layers[i]->type().c_str());
#endif
		--i;
	    }
    }

    //Rebuild blob map
    blob_map.clear();
    for (int i = 0; i < layers.size(); ++i)
    {
        for (int t = 0; t < layers[i]->top_size(); ++t)
        {
            std::string blob_name = layers[i]->top(t);
            blob_map[blob_name] = layers[i]->top_blob(blob_name);
#ifdef PRINT_SETUP_LOG
	    LOGI("Blob %s", blob_name.c_str());
            blob_map[blob_name]->PrintBlobInfo();
#endif
        }
        layers[i]->Init();
    }

    //Allocate for common mempool.
    rt_param->common_mempool()->Alloc();
    return true;
}
};
