#include "Metrics.h"

using namespace vm;

Metrics::Metrics()
{
}

Metrics::~Metrics()
{
}

void vm::Metrics::start(vk::CommandBuffer& cmd)
{
	_cmd = cmd;
	_cmd.resetQueryPool(queryPool, 0, 2);
	_cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool, 0);
}

void vm::Metrics::end(float* res)
{
	_cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool, 1);
	if (res)
		*res = getTime();
}

void Metrics::initQueryPool()
{
	gpuProps = vulkan->gpu.getProperties();
	assert(gpuProps.limits.timestampComputeAndGraphics);

	vk::QueryPoolCreateInfo qpci;
	qpci.queryType = vk::QueryType::eTimestamp;
	qpci.queryCount = 2;

	queryPool = vulkan->device.createQueryPool(qpci);

	queryTimes.resize(2, 0);
}

float Metrics::getTime()
{
	vulkan->device.getQueryPoolResults<uint64_t>(queryPool, 0, 2, queryTimes, sizeof(uint64_t), vk::QueryResultFlagBits::e64);
	return float(queryTimes[1] - queryTimes[0]) * gpuProps.limits.timestampPeriod * 1e-6f;
}

void Metrics::destroy()
{
	vulkan->device.destroyQueryPool(queryPool);
}
