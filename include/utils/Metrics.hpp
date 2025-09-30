#pragma once

#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/aggregation/default_aggregation.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector.h"
#include "opentelemetry/sdk/metrics/view/meter_selector.h"
#include "opentelemetry/sdk/metrics/view/view.h"
#include <vector>
#include <memory>
#include <atomic>

namespace metrics_sdk      = opentelemetry::sdk::metrics;
namespace metrics_api      = opentelemetry::metrics;
namespace metrics_exporter = opentelemetry::exporter::metrics;

class MetricsManager
{
public:

    MetricsManager(const MetricsManager&) = delete;
    MetricsManager& operator=(const MetricsManager&) = delete;

    static MetricsManager& GetInstance()
    {
        static MetricsManager instance;
        return instance;
    }

    // 靜態關閉方法
    static void Shutdown()
    {
        auto& instance = GetInstance();
        if (instance.is_initialized.exchange(false))
        {
            // 重置 meter (使用 nullptr 指派)
            instance.meter = opentelemetry::nostd::shared_ptr<metrics_api::Meter>(nullptr);
            
            // 關閉全域 MeterProvider
            auto noop_provider = opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(
                new opentelemetry::metrics::NoopMeterProvider()
            );
            metrics_api::Provider::SetMeterProvider(noop_provider);
            
            // 重置 provider
            instance.provider.reset();
        }
    }

    // 檢查是否已初始化
    bool IsInitialized() const
    {
        return is_initialized.load();
    }

    // 建立請求總數計數器 (Counter)
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>> CreateRequestCounter()
    {
        if (!IsInitialized())
        {
            return opentelemetry::nostd::shared_ptr<metrics_api::Counter<uint64_t>>(nullptr);
        }
        
        return meter->CreateUInt64Counter(
            "server_requests_total",
            "Total number of requests processed by the server."
        );
    }

    // 建立活躍連線數儀表 (Gauge, implemented as UpDownCounter)
    opentelemetry::nostd::shared_ptr<metrics_api::UpDownCounter<int64_t>> CreateActiveConnectionsGauge()
    {
        if (!IsInitialized())
        {
            return opentelemetry::nostd::shared_ptr<metrics_api::UpDownCounter<int64_t>>(nullptr);
        }
        
        return meter->CreateInt64UpDownCounter(
            "server_active_connections",
            "Number of active connections."
        );
    }

    // 建立請求延遲直方圖 (Histogram)
    opentelemetry::nostd::shared_ptr<metrics_api::Histogram<double>> CreateLatencyHistogram()
    {
        if (!IsInitialized())
        {
            return opentelemetry::nostd::shared_ptr<metrics_api::Histogram<double>>(nullptr);
        }

        return meter->CreateDoubleHistogram(
            "server_request_latency_seconds",
            "Request latency in seconds."
        );
    }

private:
    opentelemetry::nostd::shared_ptr<metrics_api::Meter> meter;
    std::shared_ptr<metrics_api::MeterProvider> provider;
    std::atomic<bool> is_initialized{false};

    void InitMetrics()
    {
        if (is_initialized.load())
        {
            return; // 已經初始化過了
        }
        
        try
        {
            metrics_exporter::PrometheusExporterOptions opts;
            opts.url = "localhost:9464";
            auto prometheus_exporter = metrics_exporter::PrometheusExporterFactory::Create(opts);

            // MeterProviderFactory 創建的是一個 sdk::MeterProvider 的 unique_ptr
            auto sdk_provider = metrics_sdk::MeterProviderFactory::Create();
            // 直接在 sdk::MeterProvider 上新增 MetricReader
            sdk_provider->AddMetricReader(std::move(prometheus_exporter));
 
            provider = std::shared_ptr<metrics_api::MeterProvider>(std::move(sdk_provider));
            metrics_api::Provider::SetMeterProvider(provider);

            // 建立一個 Meter，之後的所有指標都由它產生
            meter = provider->GetMeter("high-concurrency-server", "1.0.0");
            
            is_initialized.store(true);
        }
        catch (const std::exception& e)
        {
            // 初始化失敗時的處理
            is_initialized.store(false);
            throw;
        }
    }
    
    MetricsManager()
    {
        InitMetrics();
    }
    
    // 靜態物件的解構函式最好是 noexcept 且不做複雜操作
    // 清理工作由 main 函式中的顯式 Shutdown() 呼叫負責
    ~MetricsManager() noexcept = default;
};