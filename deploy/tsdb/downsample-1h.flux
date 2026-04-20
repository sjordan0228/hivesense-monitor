option task = {name: "combsense downsample 1h", every: 15m, offset: 1m}

from(bucket: "combsense")
  |> range(start: -task.every * 2, stop: -task.offset)
  |> filter(fn: (r) => r._measurement == "sensor_reading")
  |> aggregateWindow(every: 1h, fn: mean, createEmpty: false)
  |> set(key: "_measurement", value: "sensor_reading_1h")
  |> to(bucket: "combsense_1h", org: "combsense")
