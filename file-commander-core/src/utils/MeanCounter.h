#pragma once

template <typename T> class CMeanCounter
{
public:

	explicit CMeanCounter(float smoothFactor = 0.1f);
	~CMeanCounter(void);

	void operator= (const T& value);
	void reset ();

	T geometricMean() const;
	T arithmeticMean() const;
	T smoothMean () const;

private:
	T     _arithMeanAccumulator;
	T     _geomMeanAccumulator;
	float _smoothMeanAccumulator;

	float	_smoothFactor;
	size_t	_counter;
};

template <typename T>
T CMeanCounter<T>::smoothMean() const
{
	return T(_smoothMeanAccumulator);
}

template <typename T>
T CMeanCounter<T>::arithmeticMean() const
{
	return _counter > 0 ? _arithMeanAccumulator / T(_counter) : T(0);
}

template <typename T>
T CMeanCounter<T>::geometricMean() const
{
	return _counter > 0 ? T (pow (_geomMeanAccumulator, 1 / T(_counter))) : T(0);
}

template <typename T>
void CMeanCounter<T>::reset()
{
	_arithMeanAccumulator = T(0);
	_geomMeanAccumulator = T(1);
	_counter = 0;
}

template <typename T>
void CMeanCounter<T>::operator=( const T& value )
{
	++_counter;
	_arithMeanAccumulator	+= value;
	_geomMeanAccumulator	*= value;
	if (_smoothMeanAccumulator > 0.000001f)
		_smoothMeanAccumulator	+= _smoothFactor * ((float)value - _smoothMeanAccumulator);
	else
		_smoothMeanAccumulator = (float)value;
}

template <typename T>
CMeanCounter<T>::~CMeanCounter( void )
{

}

template <typename T>
CMeanCounter<T>::CMeanCounter(float smoothFactor)  :
	_arithMeanAccumulator ( T(0) ),
	_geomMeanAccumulator ( T(1) ),
	_smoothMeanAccumulator( 0.0f ),
	_smoothFactor (smoothFactor),
	_counter (0)
{

}

