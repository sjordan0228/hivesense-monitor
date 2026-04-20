option task = {name: "combsense downsample 1d", every: 6h, offset: 10m}

from(bucket: "combsense_1h")
  |> range(start: -task.every * 2, stop: -task.offset)
  |> filter(fn: (r) => r._measurement == "sensor_reading_1h")
  |> aggregateWindow(every: 1d, fn: mean, createEmpty: false)
  |> set(key: "_measurement", value: "sensor_reading_1d")
  |> to(bucket: "combsense_1d", org: "combsense")
