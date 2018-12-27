#include "Metrics.h"

using namespace vm;

Metrics::Metrics()
{
}

Metrics::~Metrics()
{
}

void Metrics::initQueryPool()
{
	gpuProps = vulkan->gpu.getProperties();
	assert(gpuProps.limits.timestampComputeAndGraphics);

	vk::QueryPoolCreateInfo qpci;
	qpci.queryType = vk::QueryType::eTimestamp;
	qpci.queryCount = 6;

	queryPool = vulkan->device.createQueryPool(qpci);

	queryTimes.resize(6, 0);
}

float Metrics::getGPUFrameTime()
{
	vulkan->device.getQueryPoolResults<uint64_t>(queryPool, 0, 6, queryTimes, sizeof(uint64_t), vk::QueryResultFlagBits::e64);
	return float(queryTimes[1] - queryTimes[0]) * gpuProps.limits.timestampPeriod * 1e-6f;
}

void Metrics::destroy()
{
	vulkan->device.destroyQueryPool(queryPool);
}
