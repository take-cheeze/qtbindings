assert('QPoint operator') do
  p1 = Qt::Point.new(5,5)
  p2 = Qt::Point.new(20,20)
  p1 + p2
  p1 - p2
  -p1 + p2
  p2 += p1
  p2 -= p1
  p2 * 3

  true
end

assert('QRegion operator') do
  r1 = Qt::Region.new()
  r2 = Qt::Region.new( 100,100,200,80, Qt::Region::Ellipse )
  r1 + r2
  true
end

assert('QMatrix operator') do
  a    = Math::PI/180 * 25         # convert 25 to radians
  sina = Math.sin(a)
  cosa = Math.cos(a)
  m1 = Qt::Matrix.new(1, 0, 0, 1, 10, -20)  # translation matrix
  m2 = Qt::Matrix.new( cosa, sina, -sina, cosa, 0, 0 )
  m3 = Qt::Matrix.new(1.2, 0, 0, 0.7, 0, 0) # scaling matrix
  m = Qt::Matrix.new
  m = m3 * m2 * m1                  # combine all transformations
  true
end
