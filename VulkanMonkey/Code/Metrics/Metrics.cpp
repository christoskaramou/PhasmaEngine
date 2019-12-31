#include "Metrics.h"

using namespace vm;

Metrics::Metrics(): gpuProps({})
{
}

Metrics::~Metrics()
{
}

void Metrics::start(const vk::CommandBuffer& cmd)
{
	_cmd = cmd;
	_cmd.resetQueryPool(queryPool, 0, 2);
	_cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPool, 0);
}

void Metrics::end(float* res)
{
	_cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool, 1);
	if (res)
		*res = getTime();
}

void Metrics::initQueryPool()
{
	gpuProps = VulkanContext::get()->gpu.getProperties();
	if (!gpuProps.limits.timestampComputeAndGraphics)
		throw std::runtime_error("Timestamps not supported");

	vk::QueryPoolCreateInfo qpci;
	qpci.queryType = vk::QueryType::eTimestamp;
	qpci.queryCount = 2;

	queryPool = VulkanContext::get()->device.createQueryPool(qpci);

	queryTimes.resize(2, 0);
}

float Metrics::getTime()
{
	const auto res = VulkanContext::get()->device.getQueryPoolResults<uint64_t>(queryPool, 0, 2, queryTimes, sizeof(uint64_t), vk::QueryResultFlagBits::e64);
	if (res != vk::Result::eSuccess)
		return 0.0f;
	return static_cast<float>(queryTimes[1] - queryTimes[0]) * gpuProps.limits.timestampPeriod * 1e-6f;
}

void Metrics::destroy() const
{
	VulkanContext::get()->device.destroyQueryPool(queryPool);
}
