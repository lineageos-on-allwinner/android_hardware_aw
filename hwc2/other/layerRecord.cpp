
#include <hardware/hwcomposer2.h>
#include <vector>

// Record all layers attaching to this display,
// So that we can determine which layer is belong to us.

struct LayerRecord {
	hwc2_display_t display;
	std::vector<hwc2_layer_t> layers;
};

static std::vector<LayerRecord*> PerDisplayRecords;

static LayerRecord* getDisplayRecord(hwc2_display_t display)
{
	for (size_t i = 0; i < PerDisplayRecords.size(); ++i) {
		if (PerDisplayRecords[i]->display == display) {
			return PerDisplayRecords[i];
		}
	}
	LayerRecord* record = new LayerRecord();
	record->display = display;
	PerDisplayRecords.push_back(record);
	return record;
}

void addLayerRecord(hwc2_display_t display, hwc2_layer_t id)
{
	LayerRecord* record = getDisplayRecord(display);
	record->layers.push_back(id);
}

bool removeLayerRecord(hwc2_display_t display, hwc2_layer_t id)
{
	LayerRecord* record = getDisplayRecord(display);

	auto iter = record->layers.begin();
	while (iter != record->layers.end()) {
		if (*iter == id) {
			record->layers.erase(iter);
			return true;
		}
		++iter;
	}
	return false;
}

